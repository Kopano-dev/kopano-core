"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import struct
import sys
import traceback
import time

from MAPI import (
    ECImportContentsChanges, SYNC_E_IGNORE, WrapStoreEntryID,
    SYNC_NORMAL, SYNC_ASSOCIATED, SYNC_CATCHUP, SYNC_UNICODE, IStream,
    STREAM_SEEK_SET, RELOP_GE, RELOP_LT, ECImportHierarchyChanges
)
from MAPI.Defs import (
    PpropFindProp
)
from MAPI.Tags import (
    PR_ENTRYID, PR_STORE_ENTRYID, PR_EC_PARENT_HIERARCHYID,
    PR_EC_HIERARCHYID, PR_STORE_RECORD_KEY, PR_CONTENTS_SYNCHRONIZER,
    PR_MESSAGE_DELIVERY_TIME, PR_HIERARCHY_SYNCHRONIZER
)
from MAPI.Tags import (
    IID_IExchangeImportContentsChanges, IID_IECImportContentsChanges,
    IID_IExchangeExportChanges, IID_IExchangeImportHierarchyChanges,
    IID_IECImportHierarchyChanges
)
from MAPI.Struct import (
    MAPIError, MAPIErrorNotFound, MAPIErrorNoAccess, SPropValue,
    SPropertyRestriction, SAndRestriction,
)
from MAPI.Time import (
    unixtime
)

from .compat import benc as _benc, bdec as _bdec

if sys.hexversion >= 0x03000000:
    from . import item as _item
    try:
        from . import folder as _folder
    except ImportError:
        _folder = sys.modules[__package__+'.folder']
    try:
        from . import store as _store
    except ImportError:
        _store = sys.modules[__package__+'.store']
    try:
        from . import utils as _utils
    except ImportError:
        _utils = sys.modules[__package__+'.utils']
else:
    import item as _item
    import folder as _folder
    import store as _store
    import utils as _utils

class TrackingHierarchyImporter(ECImportHierarchyChanges):
    def __init__(self, server, importer, log, stats):
        ECImportHierarchyChanges.__init__(self, [IID_IExchangeImportHierarchyChanges, IID_IECImportHierarchyChanges])
        self.importer = importer

    def ImportFolderChange(self, props):
        eid = _benc(PpropFindProp(props, PR_ENTRYID).Value)
        folder = self.importer.store.folder(entryid=eid)
        self.importer.update(folder)

    def ImportFolderDeletion(self, flags, sourcekeys):
        for sourcekey in sourcekeys:
            folder = _folder.Folder(self.importer.store)
            folder._sourcekey = _benc(sourcekey)
            self.importer.delete(folder, flags)

    def UpdateState(self, stream):
        pass

class TrackingContentsImporter(ECImportContentsChanges):
    def __init__(self, server, importer, log, stats):
        ECImportContentsChanges.__init__(self, [IID_IExchangeImportContentsChanges, IID_IECImportContentsChanges])
        self.server = server
        self.importer = importer
        self.log = log
        self.stats = stats
        self.skip = False

    def ImportMessageChangeAsAStream(self, props, flags):
        self.ImportMessageChange(props, flags)

    def ImportMessageChange(self, props, flags):
        if self.skip:
            raise MAPIError(SYNC_E_IGNORE)
        try:
            entryid = PpropFindProp(props, PR_ENTRYID)
            if self.importer.store:
                mapistore = self.importer.store.mapiobj
            else:
                store_entryid = PpropFindProp(props, PR_STORE_ENTRYID).Value
                store_entryid = WrapStoreEntryID(0, b'zarafa6client.dll', store_entryid[:-4]) + self.server.pseudo_url + b'\x00'
                mapistore = self.server._store2(store_entryid)
            item = _item.Item()
            item.server = self.server
            item.store = _store.Store(mapiobj=mapistore, server=self.server)
            try:
                item.mapiobj = _utils.openentry_raw(mapistore, entryid.Value, 0)
                props = item.mapiobj.GetProps([PR_EC_HIERARCHYID, PR_EC_PARENT_HIERARCHYID, PR_STORE_RECORD_KEY], 0) # XXX properties don't exist?
                item.docid = props[0].Value
                item.storeid = _benc(props[2].Value)
                if hasattr(self.importer, 'update'):
                    self.importer.update(item, flags)
            except (MAPIErrorNotFound, MAPIErrorNoAccess): # XXX, mail already deleted, can we do this in a cleaner way?
                if self.log:
                    self.log.debug('received change for entryid %s, but it could not be opened' % _benc(entryid.Value))
        except Exception:
            if self.log:
                self.log.error('could not process change for entryid %s (%r):' % (_benc(entryid.Value), props))
                self.log.error(traceback.format_exc())
            else:
                traceback.print_exc()
            if self.stats:
                self.stats['errors'] += 1
        raise MAPIError(SYNC_E_IGNORE)

    def ImportMessageDeletion(self, flags, entries):
        if self.skip:
            return
        try:
            for entry in entries:
                item = _item.Item()
                item.server = self.server
                item._sourcekey = _benc(entry)
                if hasattr(self.importer, 'delete'):
                    self.importer.delete(item, flags)
        except Exception:
            if self.log:
                self.log.error('could not process delete for entries: %s' % [_benc(entry) for entry in entries])
                self.log.error(traceback.format_exc())
            else:
                traceback.print_exc()
            if self.stats:
                self.stats['errors'] += 1

    def ImportPerUserReadStateChange(self, states):
        pass

    def UpdateState(self, stream):
        pass

def state(mapiobj, associated=False):
    exporter = mapiobj.OpenProperty(PR_CONTENTS_SYNCHRONIZER, IID_IExchangeExportChanges, 0, 0)
    if associated:
        exporter.Config(None, SYNC_NORMAL | SYNC_ASSOCIATED | SYNC_CATCHUP, None, None, None, None, 0)
    else:
        exporter.Config(None, SYNC_NORMAL | SYNC_CATCHUP, None, None, None, None, 0)
    steps, step = None, 0
    while steps != step:
        steps, step = exporter.Synchronize(step)
    stream = IStream()
    exporter.UpdateState(stream)
    stream.Seek(0, STREAM_SEEK_SET)
    return _benc(stream.Read(0xFFFFF))

def hierarchy_sync(server, syncobj, importer, state, log=None, stats=None):
    importer = TrackingHierarchyImporter(server, importer, log, stats)
    exporter = syncobj.OpenProperty(PR_HIERARCHY_SYNCHRONIZER, IID_IExchangeExportChanges, 0, 0)

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

def sync(server, syncobj, importer, state, log, max_changes, associated=False, window=None, begin=None, end=None, stats=None):
    importer = TrackingContentsImporter(server, importer, log, stats)
    exporter = syncobj.OpenProperty(PR_CONTENTS_SYNCHRONIZER, IID_IExchangeExportChanges, 0, 0)

    stream = IStream()
    stream.Write(_bdec(state))
    stream.Seek(0, STREAM_SEEK_SET)

    restriction = None
    if window:
        # sync window of last N seconds
        propval = SPropValue(PR_MESSAGE_DELIVERY_TIME, unixtime(int(time.time()) - window))
        restriction = SPropertyRestriction(RELOP_GE, PR_MESSAGE_DELIVERY_TIME, propval)

    elif begin or end:
        restrs = []
        if begin:
            propval = SPropValue(PR_MESSAGE_DELIVERY_TIME, unixtime(time.mktime(begin.timetuple())))
            restrs.append(SPropertyRestriction(RELOP_GE, PR_MESSAGE_DELIVERY_TIME, propval))
        if end:
            propval = SPropValue(PR_MESSAGE_DELIVERY_TIME, unixtime(time.mktime(end.timetuple())))
            restrs.append(SPropertyRestriction(RELOP_LT, PR_MESSAGE_DELIVERY_TIME, propval))
        if len(restrs) == 1:
            restriction = restrs[0]
        else:
            restriction = SAndRestriction(restrs)

    flags = SYNC_NORMAL | SYNC_UNICODE
    if associated:
        flags |= SYNC_ASSOCIATED
    try:
        exporter.Config(stream, flags, importer, restriction, None, None, 0)
    except MAPIErrorNotFound: # syncid purged because of 'sync_lifetime' option in server.cfg: get new syncid.
        if log:
            log.warn("Sync state does not exist on server (anymore); requesting new one")

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
                (steps, step) = exporter.Synchronize(step)
            finally:
                importer.skip = False

            changes += 1
            retry = 0

            if (steps == step) or (max_changes and changes >= max_changes):
                break

        except MAPIError as e:
            if log:
                log.warn("Received a MAPI error or timeout (error=0x%x, retry=%d/5)" % (e.hr, retry))

            time.sleep(sleep_time)

            if sleep_time < 5.0:
                sleep_time *= 2.0

            if retry < 5:
                retry += 1
            else:
                if log:
                    log.error("Too many retries, skipping change")
                if stats:
                    stats['errors'] += 1

                importer.skip = True # in case of a timeout or other issue, try to skip the change after trying several times

                retry = 0

    exporter.UpdateState(stream)

    stream.Seek(0, STREAM_SEEK_SET)
    return _benc(stream.Read(0xFFFFF))
