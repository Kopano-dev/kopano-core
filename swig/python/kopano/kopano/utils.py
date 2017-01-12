"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import codecs
import sys
import traceback
import time

from MAPI.Util import *

from .defs import *
from .errors import *

from .compat import is_int as _is_int, unhex as _unhex

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
        from .item import Item
        from .store import Store

        if self.skip:
            raise MAPIError(SYNC_E_IGNORE)
        try:
            entryid = PpropFindProp(props, PR_ENTRYID)
            if self.importer.store:
                mapistore = self.importer.store.mapiobj
            else:
                store_entryid = PpropFindProp(props, PR_STORE_ENTRYID).Value
                store_entryid = WrapStoreEntryID(0, b'zarafa6client.dll', store_entryid[:-4])+self.server.pseudo_url+b'\x00'
                mapistore = self.server.mapisession.OpenMsgStore(0, store_entryid, None, 0) # XXX cache
            item = Item()
            item.server = self.server
            item.store = Store(mapiobj=mapistore, server=self.server)
            try:
                item.mapiobj = openentry_raw(mapistore, entryid.Value, 0)
                item.folderid = PpropFindProp(props, PR_EC_PARENT_HIERARCHYID).Value
                props = item.mapiobj.GetProps([PR_EC_HIERARCHYID, PR_EC_PARENT_HIERARCHYID, PR_STORE_RECORD_KEY], 0) # XXX properties don't exist?
                item.docid = props[0].Value
                # item.folderid = props[1].Value # XXX 
                item.storeid = bin2hex(props[2].Value)
                if hasattr(self.importer, 'update'):
                    self.importer.update(item, flags)
            except (MAPIErrorNotFound, MAPIErrorNoAccess): # XXX, mail already deleted, can we do this in a cleaner way?
                if self.log:
                    self.log.debug('received change for entryid %s, but it could not be opened' % bin2hex(entryid.Value))
        except Exception:
            if self.log:
                self.log.error('could not process change for entryid %s (%r):' % (bin2hex(entryid.Value), props))
                self.log.error(traceback.format_exc())
            else:
                traceback.print_exc()
            if self.stats:
                self.stats['errors'] += 1
        raise MAPIError(SYNC_E_IGNORE)

    def ImportMessageDeletion(self, flags, entries):
        from .item import Item

        if self.skip:
            return
        try:
            for entry in entries:
                item = Item()
                item.server = self.server
                item._sourcekey = bin2hex(entry)
                if hasattr(self.importer, 'delete'):
                    self.importer.delete(item, flags)
        except Exception:
            if self.log:
                self.log.error('could not process delete for entries: %s' % [bin2hex(entry) for entry in entries])
                self.log.error(traceback.format_exc())
            else:
                traceback.print_exc()
            if self.stats:
                self.stats['errors'] += 1

    def ImportPerUserReadStateChange(self, states):
        pass

    def UpdateState(self, stream):
        pass


def stream(mapiobj, proptag):
    stream = mapiobj.OpenProperty(proptag, IID_IStream, 0, 0)

    if proptag == PR_RTF_COMPRESSED:
        stream = WrapCompressedRTFStream(stream, 0)

    block_size = 0x100000 # 1MB

    data = []
    while True:
        temp = stream.Read(block_size)
        data.append(temp)

        if len(temp) < block_size:
            break

    data = b''.join(data)

    if PROP_TYPE(proptag) == PT_UNICODE:
        data = data.decode('utf-32le') # under windows them be utf-16le?

    return data

def create_prop(self, mapiobj, proptag, value, proptype=None):
    if _is_int(proptag):
        if PROP_TYPE(proptag) == PT_SYSTIME:
            value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
        # handle invalid type versus value. For example proptype=PT_UNICODE and value=True
        try:
            mapiobj.SetProps([SPropValue(proptag, value)])
        except TypeError:
            raise Error('Could not create property, type and value did not match')
    else: # named prop
        # XXX: code duplication from prop()
        namespace, name = proptag.split(':') # XXX syntax
        if name.isdigit(): # XXX
            name = int(name)

        if proptype == PT_SYSTIME:
            value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
        if not proptype:
            raise Error('Missing type to create named Property') # XXX exception too general?

        nameid = MAPINAMEID(NAMESPACE_GUID.get(namespace), MNID_ID if isinstance(name, int) else MNID_STRING, name)
        lpname = mapiobj.GetIDsFromNames([nameid], 0)
        proptag = CHANGE_PROP_TYPE(lpname[0], proptype)
        # handle invalid type versus value. For example proptype=PT_UNICODE and value=True
        try:
            mapiobj.SetProps([SPropValue(proptag, value)])
        except TypeError:
            raise Error('Could not create property, type and value did not match')

    return prop(self, mapiobj, proptag)

def prop(self, mapiobj, proptag, create=False):
    if _is_int(proptag):
        try:
            sprop = HrGetOneProp(mapiobj, proptag)
        except MAPIErrorNotEnoughMemory:
            data = stream(mapiobj, proptag)
            sprop = SPropValue(proptag, data)
        except MAPIErrorNotFound as e:
            if create and PROP_TYPE(proptag) == PT_LONG: # XXX generalize!
                mapiobj.SetProps([SPropValue(proptag, 0)])
                mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
                sprop = HrGetOneProp(mapiobj, proptag)
            else:
                raise e
        from .prop import Property
        return Property(mapiobj, sprop)
    else:
        namespace, name = proptag.split(':') # XXX syntax
        if name.isdigit(): # XXX
            name = int(name)

        for prop in self.props(namespace=namespace): # XXX sloow, streaming
            if prop.name == name:
                return prop
        raise MAPIErrorNotFound()

def props(mapiobj, namespace=None):
    proptags = mapiobj.GetPropList(MAPI_UNICODE)
    sprops = mapiobj.GetProps(proptags, MAPI_UNICODE)
    from .prop import Property
    props = [Property(mapiobj, sprop) for sprop in sprops]
    for p in sorted(props):
        if not namespace or p.namespace == namespace:
            yield p

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
    stream.Seek(0, MAPI.STREAM_SEEK_SET)
    return bin2hex(stream.Read(0xFFFFF))

def sync(server, syncobj, importer, state, log, max_changes, associated=False, window=None, begin=None, end=None, stats=None):
    importer = TrackingContentsImporter(server, importer, log, stats)
    exporter = syncobj.OpenProperty(PR_CONTENTS_SYNCHRONIZER, IID_IExchangeExportChanges, 0, 0)

    stream = IStream()
    stream.Write(_unhex(state))
    stream.Seek(0, MAPI.STREAM_SEEK_SET)

    restriction = None
    if window:
        # sync window of last N seconds
        propval = SPropValue(PR_MESSAGE_DELIVERY_TIME, MAPI.Time.unixtime(int(time.time()) - window))
        restriction = SPropertyRestriction(RELOP_GE, PR_MESSAGE_DELIVERY_TIME, propval)

    elif begin or end:
        restrs = []
        if begin:
            propval = SPropValue(PR_MESSAGE_DELIVERY_TIME, MAPI.Time.unixtime(time.mktime(begin.timetuple())))
            restrs.append(SPropertyRestriction(RELOP_GE, PR_MESSAGE_DELIVERY_TIME, propval))
        if end:
            propval = SPropValue(PR_MESSAGE_DELIVERY_TIME, MAPI.Time.unixtime(time.mktime(end.timetuple())))
            restrs.append(SPropertyRestriction(RELOP_LT, PR_MESSAGE_DELIVERY_TIME, propval))
        if len(restrs) == 1:
            restriction = restrs[0]
        else:
            restriction = SAndRestriction(restrs)

    flags = SYNC_NORMAL | SYNC_UNICODE
    if associated:
        flags |= SYNC_ASSOCIATED
    try:
        exporter.Config(stream, SYNC_NORMAL | SYNC_ASSOCIATED | SYNC_UNICODE, importer, restriction, None, None, 0)
    except MAPIErrorNotFound: # syncid purged because of 'sync_lifetime' option in server.cfg: get new syncid.
        if log:
            log.warn("Sync state does not exist on server (anymore); requesting new one")

        syncid, changeid = struct.unpack('<II', state.decode('hex'))
        stream = IStream()
        stream.Write(struct.pack('<II', 0, changeid))
        stream.Seek(0, MAPI.STREAM_SEEK_SET)

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

    stream.Seek(0, MAPI.STREAM_SEEK_SET)
    return bin2hex(stream.Read(0xFFFFF))

def openentry_raw(mapistore, entryid, flags): # avoid underwater action for archived items
    try:
        return mapistore.OpenEntry(entryid, IID_IECMessageRaw, flags)
    except MAPIErrorInterfaceNotSupported:
        return mapistore.OpenEntry(entryid, None, flags)

def bestbody(mapiobj): # XXX we may want to use the swigged version in libcommon, once available
    # apparently standardized method for determining original message type!
    tag = PR_NULL
    props = mapiobj.GetProps([PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED, PR_RTF_IN_SYNC], 0)

    if (props[3].ulPropTag != PR_RTF_IN_SYNC): # XXX why..
        return tag

    # MAPI_E_NOT_ENOUGH_MEMORY indicates the property exists, but has to be streamed
    if((props[0].ulPropTag == PR_BODY_W or (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY)) and
       (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and props[1].Value == MAPI_E_NOT_FOUND) and
       (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and props[2].Value == MAPI_E_NOT_FOUND)):
        tag = PR_BODY_W

    # XXX why not just check MAPI_E_NOT_FOUND..?
    elif((props[1].ulPropTag == PR_HTML or (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and props[1].Value == MAPI_E_NOT_ENOUGH_MEMORY)) and
         (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
         (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and props[2].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
         props[3].Value == False):
        tag = PR_HTML

    elif((props[2].ulPropTag == PR_RTF_COMPRESSED or (PROP_TYPE(props[2].ulPropTag) == PT_ERROR and props[2].Value == MAPI_E_NOT_ENOUGH_MEMORY)) and
         (PROP_TYPE(props[0].ulPropTag) == PT_ERROR and props[0].Value == MAPI_E_NOT_ENOUGH_MEMORY) and
         (PROP_TYPE(props[1].ulPropTag) == PT_ERROR and props[1].Value == MAPI_E_NOT_FOUND) and
         props[3].Value == True):
        tag = PR_RTF_COMPRESSED

    return tag

def unpack_short(s, pos):
    return struct.unpack_from('<H', s, pos)[0]

def unpack_long(s, pos):
    return struct.unpack_from('<L', s, pos)[0]

def unpack_string(s, pos, length):
    return b''.join(struct.unpack_from('<' + 's' * length, s, pos))

def pack_long(i):
    return struct.pack('<L', i)

def rectime_to_unixtime(t):
    return (t - 194074560) * 60

def unixtime_to_rectime(t):
    return int(t/60) + 194074560

def extract_ipm_ol2007_entryids(blob, offset):
    # Extracts entryids from PR_IPM_OL2007_ENTRYIDS blob using
    # logic from common/Util.cpp Util::ExtractAdditionalRenEntryID.
    pos = 0
    while True:
        blocktype = unpack_short(blob, pos)
        if blocktype == 0:
            break
        pos += 2

        totallen = unpack_short(blob, pos)
        pos += 2

        if blocktype == offset:
            pos += 2 # skip check
            sublen = unpack_short(blob, pos)
            pos += 2
            return codecs.encode(blob[pos:pos+sublen], 'hex').upper()
        else:
            pos += totallen

def permissions(obj):
        from .table import Table
        from .permission import Permission

        try:
            acl_table = obj.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
        except MAPIErrorNotFound:
            return
        table = Table(obj.server, acl_table.GetTable(0), PR_ACL_TABLE)
        for row in table.dict_rows():
            yield Permission(acl_table, row, obj.server)

def permission(obj, member, create):
        from .user import User
        from .group import Group

        for permission in obj.permissions():
            if permission.member == member:
                return permission
        if create:
            acl_table = obj.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
            if isinstance(member, User): # XXX *.id_ or something..?
                memberid = member.userid
            elif isinstance(member, Group):
                memberid = member.groupid
            else:
                memberid = member.companyid
            acl_table.ModifyTable(0, [ROWENTRY(ROW_ADD, [SPropValue(PR_MEMBER_ENTRYID, _unhex(memberid)), SPropValue(PR_MEMBER_RIGHTS, 0)])])
            return obj.permission(member)
        else:
            raise NotFoundError("no permission entry for '%s'" % member.name)

def bytes_to_human(b):
    suffixes = ['b', 'kb', 'mb', 'gb', 'tb', 'pb']
    if b == 0: return '0 b'
    i = 0
    len_suffixes = len(suffixes)-1
    while b >= 1024 and i < len_suffixes:
        b /= 1024
        i += 1
    f = ('%.2f' % b).rstrip('0').rstrip('.')
    return '%s %s' % (f, suffixes[i])

def human_to_bytes(s):
    """
    Author: Giampaolo Rodola' <g.rodola [AT] gmail [DOT] com>
    License: MIT
    """
    s = s.lower()
    init = s
    num = ""
    while s and s[0:1].isdigit() or s[0:1] == '.':
        num += s[0]
        s = s[1:]
    num = float(num)
    letter = s.strip()
    for sset in [('b', 'k', 'm', 'g', 't', 'p', 'e', 'z', 'y'),
                 ('b', 'kb', 'mb', 'gb', 'tb', 'pb', 'eb', 'zb', 'yb'),
                 ('b', 'kib', 'mib', 'gib', 'tib', 'pib', 'eib', 'zib', 'yib')]:
        if letter in sset:
            break
    else:
        raise ValueError("can't interpret %r" % init)
    prefix = {sset[0]:1}
    for i, s in enumerate(sset[1:]):
        prefix[s] = 1 << (i+1)*10
    return int(num * prefix[letter])
