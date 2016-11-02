"""
High-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 Kopano and its licensors (see LICENSE file for details)

Some goals:

- To be fully object-oriented, pythonic, layer above MAPI
- To be usable for many common system administration tasks
- To provide full access to the underlying MAPI layer if needed
- To return all text as unicode strings
- To return/accept binary identifiers in readable (hex-encoded) form
- To raise well-described exceptions if something goes wrong

Main classes:

:class:`Server`

:class:`Store`

:class:`User`

:class:`Company`

:class:`Store`

:class:`Folder`

:class:`Item`

:class:`Body`

:class:`Attachment`

:class:`Address`

:class:`Outofoffice`

:class:`Quota`

:class:`Permission`

:class:`Config`

:class:`Service`


"""

import codecs
import collections
import contextlib
try:
    import cPickle as pickle
except ImportError:
    import pickle
import csv
try:
    import daemon
    import daemon.pidlockfile
except ImportError:
    pass
import errno
import fnmatch
import datetime
from functools import wraps
import grp
try:
    import libcommon # XXX distribute with python-mapi? or rewrite functionality here?
except ImportError:
    pass
import logging.handlers
from multiprocessing import Process, Queue
try:
    from Queue import Empty
except ImportError:
    from queue import Empty
import optparse
import os.path
import pwd
import socket
import sys
try:
    from StringIO import StringIO
except ImportError:
    from io import StringIO
import struct
import threading
import traceback
import mailbox
import email.parser
import email.utils
import re
import signal
import ssl
import time

from MAPI.Util import *
from MAPI.Util.Generators import *
import MAPI.Util.AddressBook
import MAPI.Tags
import MAPI.Time
import _MAPICore
import inetmapi
import icalmapi

try:
    REV_TYPE
except NameError:
    REV_TYPE = {}
    for K, V in _MAPICore.__dict__.items():
        if K.startswith('PT_'):
            REV_TYPE[V] = K

try:
    REV_TAG
except NameError:
    REV_TAG = {}
    for K in sorted(MAPI.Tags.__dict__, reverse=True):
        if K.startswith('PR_'):
            REV_TAG[MAPI.Tags.__dict__[K]] = K

PS_INTERNET_HEADERS = DEFINE_OLEGUID(0x00020386, 0, 0)
PS_EC_IMAP = DEFINE_GUID(0x00f5f108, 0x8e3f, 0x46c7, 0xaf, 0x72, 0x5e, 0x20, 0x1c, 0x23, 0x49, 0xe7)
PSETID_Archive = DEFINE_GUID(0x72e98ebc, 0x57d2, 0x4ab5, 0xb0, 0xaa, 0xd5, 0x0a, 0x7b, 0x53, 0x1c, 0xb9)
PSETID_Appointment = DEFINE_OLEGUID(0x00062002, 0, 0)
PSETID_Task = DEFINE_OLEGUID(0x00062003, 0, 0)
PSETID_Address = DEFINE_OLEGUID(0x00062004, 0, 0)
PSETID_Common = DEFINE_OLEGUID(0x00062008, 0, 0)
PSETID_Log = DEFINE_OLEGUID(0x0006200A, 0, 0)
PSETID_Note = DEFINE_OLEGUID(0x0006200E, 0, 0)
PSETID_Meeting = DEFINE_GUID(0x6ED8DA90, 0x450B, 0x101B,0x98, 0xDA, 0x00, 0xAA, 0x00, 0x3F, 0x13, 0x05)

NAMED_PROPS_INTERNET_HEADERS = [
    MAPINAMEID(PS_INTERNET_HEADERS, MNID_STRING, u'x-original-to'),
]

NAMED_PROPS_ARCHIVER = [
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'store-entryids'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'item-entryids'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'stubbed'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'ref-store-entryid'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'ref-item-entryid'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'ref-prev-entryid'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'flags')
]

NAMED_PROP_CATEGORY = MAPINAMEID(PS_PUBLIC_STRINGS, MNID_STRING, u'Keywords')

GUID_NAMESPACE = {
    PSETID_Archive: 'archive',
    PSETID_Common: 'common',
    PSETID_Appointment: 'appointment',
    PSETID_Task: 'task',
    PSETID_Address: 'address',
    PSETID_Log: 'log',
    PS_INTERNET_HEADERS: 'internet_headers',
    PSETID_Meeting: 'meeting',
    PS_EC_IMAP: 'imap',
}
NAMESPACE_GUID = dict((b,a) for (a,b) in GUID_NAMESPACE.items())

# XXX copied from common/ECDefs.h
def OBJECTCLASS(__type, __class):
    return (__type << 16) | (__class & 0xFFFF)

OBJECTTYPE_MAILUSER = 1
ACTIVE_USER = OBJECTCLASS(OBJECTTYPE_MAILUSER, 1)
NONACTIVE_USER = OBJECTCLASS(OBJECTTYPE_MAILUSER, 2)

# XXX copied from msr/main.py
MUIDECSAB = DEFINE_GUID(0x50a921ac, 0xd340, 0x48ee, 0xb3, 0x19, 0xfb, 0xa7, 0x53, 0x30, 0x44, 0x25)
def DEFINE_ABEID(type, id):
    return struct.pack("4B16s3I4B", 0, 0, 0, 0, MUIDECSAB, 0, type, id, 0, 0, 0, 0)
EID_EVERYONE = DEFINE_ABEID(MAPI_DISTLIST, 1)

ADDR_PROPS = [
    (PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, PR_ENTRYID, PR_DISPLAY_NAME_W, PR_SEARCH_KEY),
    (PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID, PR_SENDER_NAME_W, PR_SENDER_SEARCH_KEY),
    (PR_RECEIVED_BY_ADDRTYPE_W, PR_RECEIVED_BY_EMAIL_ADDRESS_W, PR_RECEIVED_BY_ENTRYID, PR_RECEIVED_BY_NAME_W, PR_RECEIVED_BY_SEARCH_KEY),
    (PR_ORIGINAL_SENDER_ADDRTYPE_W, PR_ORIGINAL_SENDER_EMAIL_ADDRESS_W, PR_ORIGINAL_SENDER_ENTRYID, PR_ORIGINAL_SENDER_NAME_W, PR_ORIGINAL_SENDER_SEARCH_KEY),
    (PR_ORIGINAL_AUTHOR_ADDRTYPE_W, PR_ORIGINAL_AUTHOR_EMAIL_ADDRESS_W, PR_ORIGINAL_AUTHOR_ENTRYID, PR_ORIGINAL_AUTHOR_NAME_W, PR_ORIGINAL_AUTHOR_SEARCH_KEY),
    (PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_SEARCH_KEY),
    (PR_RCVD_REPRESENTING_ADDRTYPE_W, PR_RCVD_REPRESENTING_EMAIL_ADDRESS_W, PR_RCVD_REPRESENTING_ENTRYID, PR_RCVD_REPRESENTING_NAME_W, PR_RCVD_REPRESENTING_SEARCH_KEY),
]

# Common/RecurrenceState.h
# Defines for recurrence exceptions
ARO_SUBJECT =	0x0001
ARO_MEETINGTYPE = 0x0002
ARO_REMINDERDELTA = 0x0004
ARO_REMINDERSET	= 0x0008
ARO_LOCATION = 0x0010
ARO_BUSYSTATUS	= 0x0020
ARO_ATTACHMENT = 0x0040
ARO_SUBTYPE = 0x0080
ARO_APPTCOLOR = 0x0100
ARO_EXCEPTIONAL_BODY = 0x0200

# location of entryids in PR_IPM_OL2007_ENTRYIDS
RSF_PID_RSS_SUBSCRIPTION = 0x8001
RSF_PID_SUGGESTED_CONTACTS = 0x8008

ENGLISH_FOLDER_MAP = { # XXX should we make the names pretty much identical, except for case?
    'Inbox': 'inbox',
    'Drafts': 'drafts',
    'Outbox': 'outbox',
    'Sent Items': 'sentmail',
    'Deleted Items': 'wastebasket',
    'Junk E-mail': 'junk',
    'Calendar': 'calendar',
    'Contacts': 'contacts',
    'Tasks': 'tasks',
    'Notes': 'notes',
}

UNESCAPED_SLASH_RE = re.compile(r'(?<!\\)/')

RIGHT_NAME = {
    0x1: 'read_items',
    0x2: 'create_items',
    0x80: 'create_subfolders',
    0x8: 'edit_own',
    0x20: 'edit_all',
    0x10: 'delete_own',
    0x40: 'delete_all',
    0x100: 'folder_owner',
    0x200: 'folder_contact',
    0x400: 'folder_visible',
}

NAME_RIGHT = dict((b,a) for (a,b) in RIGHT_NAME.items())

def _stream(mapiobj, proptag):
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

def _create_prop(self, mapiobj, proptag, value, proptype=None):
    if _is_int(proptag):
        if PROP_TYPE(proptag) == PT_SYSTIME:
            value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
        # handle invalid type versus value. For example proptype=PT_UNICODE and value=True
        try:
            mapiobj.SetProps([SPropValue(proptag, value)])
        except TypeError:
            raise Error('Could not create property, type and value did not match')
    else: # named prop
        # XXX: code duplication from _prop()
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

    return _prop(self, mapiobj, proptag)

def _prop(self, mapiobj, proptag):
    if _is_int(proptag):
        try:
            sprop = HrGetOneProp(mapiobj, proptag)
        except MAPIErrorNotEnoughMemory:
            data = _stream(mapiobj, proptag)
            sprop = SPropValue(proptag, data)
        return Property(mapiobj, sprop)
    else:
        namespace, name = proptag.split(':') # XXX syntax
        if name.isdigit(): # XXX
            name = int(name)

        for prop in self.props(namespace=namespace): # XXX sloow, streaming
            if prop.name == name:
                return prop
        raise MAPIErrorNotFound()

def _props(mapiobj, namespace=None):
    proptags = mapiobj.GetPropList(MAPI_UNICODE)
    sprops = mapiobj.GetProps(proptags, MAPI_UNICODE)
    props = [Property(mapiobj, sprop) for sprop in sprops]
    for p in sorted(props):
        if not namespace or p.namespace == namespace:
            yield p

def _state(mapiobj, associated=False):
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

def _sync(server, syncobj, importer, state, log, max_changes, associated=False, window=None, begin=None, end=None, stats=None):
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

    if associated:
        exporter.Config(stream, SYNC_NORMAL | SYNC_ASSOCIATED | SYNC_UNICODE, importer, restriction, None, None, 0)
    else:
        exporter.Config(stream, SYNC_NORMAL | SYNC_UNICODE, importer, restriction, None, None, 0)

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

def _openentry_raw(mapistore, entryid, flags): # avoid underwater action for archived items
    try:
        return mapistore.OpenEntry(entryid, IID_IECMessageRaw, flags)
    except MAPIErrorInterfaceNotSupported:
        return mapistore.OpenEntry(entryid, None, flags)

def _bestbody(mapiobj): # XXX we may want to use the swigged version in libcommon, once available
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

def _unpack_short(s, pos):
    return struct.unpack_from('<H', s, pos)[0]

def _unpack_long(s, pos):
    return struct.unpack_from('<L', s, pos)[0]

def _unpack_string(s, pos, length):
    return b''.join(struct.unpack_from('<' + 's' * length, s, pos))

def _pack_long(i):
    return struct.pack('<L', i)

def _rectime_to_unixtime(t):
    return (t - 194074560) * 60

def _unixtime_to_rectime(t):
    return int(t/60) + 194074560

def _extract_ipm_ol2007_entryids(blob, offset):
    # Extracts entryid's from PR_IPM_OL2007_ENTRYIDS blob using
    # logic from common/Util.cpp Util::ExtractAdditionalRenEntryID.
    pos = 0
    while True:
        blocktype = _unpack_short(blob, pos)
        if blocktype == 0:
            break
        pos += 2

        totallen = _unpack_short(blob, pos)
        pos += 2

        if blocktype == offset:
            pos += 2 # skip check
            sublen = _unpack_short(blob, pos)
            pos += 2
            return codecs.encode(blob[pos:pos+sublen], 'hex').upper()
        else:
            pos += totallen

if sys.hexversion >= 0x03000000:
    def _is_int(i):
        return isinstance(i, int)

    def _is_str(s):
        return isinstance(s, str)

    def unicode(s):
        return str(s)

    def _decode(s):
        return s

    def _repr(o):
        return o.__unicode__()

    def _unhex(s):
        return codecs.decode(s, 'hex')

    def _pickle_load(f):
        return pickle.load(f, encoding='bytes')

    def _pickle_loads(s):
        return pickle.loads(s, encoding='bytes')

else:
    def _is_int(i):
        return isinstance(i, (int, long))

    def _is_str(s):
        return isinstance(s, (str, unicode))

    def _decode(s):
        return s.decode(getattr(sys.stdin, 'encoding', 'utf8') or 'utf8')

    def _encode(s):
        return s.encode(getattr(sys.stdout, 'encoding', 'utf8') or 'utf8') # sys.stdout can be StringIO (nosetests)

    def _repr(o):
        return _encode(unicode(o))

    def _unhex(s):
        return s.decode('hex')

    def _pickle_load(f):
        return pickle.loads(f)

    def _pickle_loads(s):
        return pickle.loads(s)

def _permissions(obj):
        try:
            acl_table = obj.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
        except MAPIErrorNotFound:
            return
        table = Table(obj.server, acl_table.GetTable(0), PR_ACL_TABLE)
        for row in table.dict_rows():
            yield Permission(acl_table, row, obj.server)

def _permission(obj, member, create):
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

class Error(Exception):
    pass

class ConfigError(Error):
    pass

class NotFoundError(Error):
    pass

class LogonError(Error):
    pass

class NotSupportedError(Error):
    pass

class PersistentList(list):
    def __init__(self, mapiobj, proptag, *args, **kwargs):
        self.mapiobj = mapiobj
        self.proptag = proptag
        for attr in ('append', 'extend', 'insert', 'pop', 'remove', 'reverse', 'sort'):
            setattr(self, attr, self._autosave(getattr(self, attr)))
        list.__init__(self, *args, **kwargs)
    def _autosave(self, func):
        @wraps(func)
        def _func(*args, **kwargs):
            ret = func(*args, **kwargs)
            self.mapiobj.SetProps([SPropValue(self.proptag, self)])
            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
            return ret
        return _func

def _timed_cache(seconds=0, minutes=0, hours=0, days=0):
    # used with permission from will mcgugan, https://www.willmcgugan.com
    time_delta = datetime.timedelta(seconds=seconds, minutes=minutes, hours=hours, days=days)

    def decorate(f):
        f._updates = {}
        f._results = {}

        def do_cache(*args, **kwargs):
            key = tuple(sorted(kwargs.items()))

            updates = f._updates
            results = f._results

            t = datetime.datetime.now()
            updated = updates.get(key, t)

            if key not in results or t-updated > time_delta:
                # calculate
                updates[key] = t
                result = f(*args, **kwargs)
                results[key] = result
                return result
            else:
                # cache
                return results[key]
        return do_cache
    return decorate

class SPropDelayedValue(SPropValue):
    def __init__(self, mapiobj, proptag):
        self.mapiobj = mapiobj
        self.ulPropTag = proptag
        self._Value = None

    @property
    def Value(self):
        if self._Value is None:
            try:
                self._Value = _stream(self.mapiobj, self.ulPropTag)
            except MAPIErrorNotFound: # XXX eg normalized subject streaming broken..?
                self._Value = None
        return self._Value

class Property(object):
    """Property class"""

    def __init__(self, parent_mapiobj, mapiobj): # XXX rethink attributes, names.. add guidname..?
        self._parent_mapiobj = parent_mapiobj
        self.proptag = mapiobj.ulPropTag

        if PROP_TYPE(mapiobj.ulPropTag) == PT_ERROR and mapiobj.Value == MAPI_E_NOT_ENOUGH_MEMORY:
            for proptype in (PT_BINARY, PT_UNICODE): # XXX slow, incomplete?
                proptag = (mapiobj.ulPropTag & 0xffff0000) | proptype
                try:
                    HrGetOneProp(parent_mapiobj, proptag) # XXX: Unicode issue?? calls GetProps([proptag], 0)
                    self.proptag = proptag # XXX isn't it strange we can get here
                except MAPIErrorNotEnoughMemory:
                    mapiobj = SPropDelayedValue(parent_mapiobj, proptag)
                    self.proptag = proptag
                    break
                except MAPIErrorNotFound:
                    pass

        self.id_ = self.proptag >> 16
        self.mapiobj = mapiobj
        self._value = None

        self.idname = REV_TAG.get(self.proptag) # XXX slow, often unused: make into properties?
        self.type_ = PROP_TYPE(self.proptag)
        self.typename = REV_TYPE.get(self.type_)
        self.named = False
        self.kind = None
        self.kindname = None
        self.guid = None
        self.name = None
        self.namespace = None

        if self.id_ >= 0x8000: # possible named prop
            try:
                lpname = self._parent_mapiobj.GetNamesFromIDs([self.proptag], None, 0)[0]
                if lpname:
                    self.guid = bin2hex(lpname.guid)
                    self.namespace = GUID_NAMESPACE.get(lpname.guid)
                    self.name = lpname.id
                    self.kind = lpname.kind
                    self.kindname = 'MNID_STRING' if lpname.kind == MNID_STRING else 'MNID_ID'
                    self.named = True
            except MAPIErrorNoSupport: # XXX user.props()?
                pass

    def get_value(self):
        if self._value is None:
            if self.type_ == PT_SYSTIME: # XXX generalize, property?
                #
                # The datetime object is of "naive" type, has local time and
                # no TZ info. :-(
                #
                try:
                    self._value = datetime.datetime.fromtimestamp(self.mapiobj.Value.unixtime)
                except ValueError: # Y10K: datetime is limited to 4-digit years
                    self._value = datetime.datetime(9999, 1, 1)
            else:
                self._value = self.mapiobj.Value
        return self._value

    def set_value(self, value):
        self._value = value
        if self.type_ == PT_SYSTIME:
            # Timezones are handled.
            value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
        self._parent_mapiobj.SetProps([SPropValue(self.proptag, value)])
        self._parent_mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
    value = property(get_value, set_value)

    @property
    def strid(self):
        if self.named:
            return u'%s:%s' % (self.namespace, self.name)
        else:
            return self.idname or u'' # FIXME: should never be None

    @property
    def strval(self):
        def flatten(v):
            if isinstance(v, list):
                return u','.join(flatten(e) for e in v)
            elif isinstance(v, bool):
                return u'01'[v]
            elif self.type_ == PT_BINARY:
                return codecs.encode(v, 'hex').decode('ascii').upper()
            elif self.type_ == PT_MV_BINARY:
                return u'PT_MV_BINARY()' # XXX
            else:
                return unicode(v)
        return flatten(self.value)

    def __lt__(self, prop):
        return self.proptag < prop.proptag

    def __unicode__(self):
        return u'Property(%s)' % self.strid

    # TODO: check if data is binary and convert it to hex
    def __repr__(self):
        return _repr(self)

class Table(object):
    """Table class"""

    def __init__(self, server, mapitable, proptag, restriction=None, order=None, columns=None):
        self.server = server
        self.mapitable = mapitable
        self.proptag = proptag
        if columns:
            mapitable.SetColumns(columns, 0)
        else:
            cols = mapitable.QueryColumns(TBL_ALL_COLUMNS) # some columns are hidden by default XXX result (if at all) depends on table implementation 
            cols = cols or mapitable.QueryColumns(0) # fall-back 
            mapitable.SetColumns(cols, 0)

    @property
    def header(self):
        return [unicode(REV_TAG.get(c, hex(c))) for c in self.mapitable.QueryColumns(0)]

    def rows(self):
        try:
            for row in self.mapitable.QueryRows(-1, 0):
                yield [Property(self.server.mapistore, c) for c in row]
        except MAPIErrorNotFound:
            pass

    def dict_rows(self):
        for row in self.mapitable.QueryRows(-1, 0):
            yield dict((c.ulPropTag, c.Value) for c in row)

    def dict_(self, key, value):
        d = {}
        for row in self.mapitable.QueryRows(-1, 0):
            d[PpropFindProp(row, key).Value] = PpropFindProp(row, value).Value
        return d

    def index(self, key):
        d = {}
        for row in self.mapitable.QueryRows(-1, 0):
            d[PpropFindProp(row, key).Value] = dict((c.ulPropTag, c.Value) for c in row)
        return d

    def data(self, header=False):
        data = [[p.strval for p in row] for row in self.rows()]
        if header:
            data = [self.header] + data
        return data

    def text(self, borders=False):
        result = []
        data = self.data(header=True)
        colsizes = [max(len(d[i]) for d in data) for i in range(len(data[0]))]
        for d in data:
            line = []
            for size, c in zip(colsizes, d):
                line.append(c.ljust(size))
            result.append(' '.join(line))
        return '\n'.join(result)

    def csv(self, *args, **kwargs):
        csvfile = StringIO()
        writer = csv.writer(csvfile, *args, **kwargs)
        writer.writerows(self.data(header=True))
        return csvfile.getvalue()

    def sort(self, tags):
        if not isinstance(tags, tuple):
            tags = (tags,)
        self.mapitable.SortTable(SSortOrderSet([SSort(abs(tag), TABLE_SORT_DESCEND if tag < 0 else TABLE_SORT_ASCEND) for tag in tags], 0, 0), 0)

    def __iter__(self):
        return self.rows()

    def __repr__(self):
        return u'Table(%s)' % REV_TAG.get(self.proptag)

class Server(object):
    """Server class

    By default, tries to connect to a storage server as configured in ``/etc/kopano/admin.cfg`` or
    at UNIX socket ``/var/run/kopano/server.sock``

    Looks at command-line to see if another server address or other related options were given (such as -c, -s, -k, -p)

    :param server_socket: similar to 'server_socket' option in config file
    :param sslkey_file: similar to 'sslkey_file' option in config file
    :param sslkey_pass: similar to 'sslkey_pass' option in config file
    :param config: path of configuration file containing common server options, for example ``/etc/kopano/admin.cfg``
    :param auth_user: username to user for user authentication
    :param auth_pass: password to use for user authentication
    :param log: logger object to receive useful (debug) information
    :param options: OptionParser instance to get settings from (see :func:`parser`)
    """

    def __init__(self, options=None, config=None, sslkey_file=None, sslkey_pass=None, server_socket=None, auth_user=None, auth_pass=None, log=None, service=None, mapisession=None):
        self.options = options
        self.config = config
        self.sslkey_file = sslkey_file
        self.sslkey_pass = sslkey_pass
        self.server_socket = server_socket
        self.service = service
        self.log = log
        self.mapisession = mapisession
        self._store_cache = {}

        if not self.mapisession:
            # get cmd-line options
            if not self.options:
                self.options, args = parser().parse_args()

            # determine config file
            if config:
                pass
            elif getattr(self.options, 'config_file', None):
                config_file = os.path.abspath(self.options.config_file)
                config = globals()['Config'](None, filename=self.options.config_file) # XXX snarf
            else:
                config_file = '/etc/kopano/admin.cfg'
                try:
                    open(config_file) # check if accessible
                    config = globals()['Config'](None, filename=config_file) # XXX snarf
                except IOError:
                    pass
            self.config = config

            # get defaults
            if os.getenv("KOPANO_SOCKET"): # env variable used in testset
                self.server_socket = os.getenv("KOPANO_SOCKET")
            elif config:
                if not (server_socket or getattr(self.options, 'server_socket')): # XXX generalize
                    self.server_socket = config.get('server_socket')
                    self.sslkey_file = config.get('sslkey_file')
                    self.sslkey_pass = config.get('sslkey_pass')
            self.server_socket = self.server_socket or "default:"

            # override with explicit or command-line args
            self.server_socket = server_socket or getattr(self.options, 'server_socket', None) or self.server_socket
            self.sslkey_file = sslkey_file or getattr(self.options, 'sslkey_file', None) or self.sslkey_file
            self.sslkey_pass = sslkey_pass or getattr(self.options, 'sslkey_pass', None) or self.sslkey_pass

            # make actual connection. in case of service, wait until this succeeds.
            self.auth_user = auth_user or getattr(self.options, 'auth_user', None) or 'SYSTEM' # XXX override with args
            self.auth_pass = auth_pass or getattr(self.options, 'auth_pass', None) or ''
            while True:
                try:
                    self.mapisession = OpenECSession(self.auth_user, self.auth_pass, self.server_socket, sslkey_file=self.sslkey_file, sslkey_pass=self.sslkey_pass) #, providers=['ZARAFA6','ZCONTACTS'])
                    break
                except (MAPIErrorNetworkError, MAPIErrorDiskError):
                    if service:
                        service.log.warn("could not connect to server at '%s', retrying in 5 sec" % self.server_socket)
                        time.sleep(5)
                    else:
                        raise Error("could not connect to server at '%s'" % self.server_socket)
                except MAPIErrorLogonFailed:
                    raise LogonError('Could not logon to server: username or password incorrect')

        # start talking dirty
        self.mapistore = GetDefaultStore(self.mapisession)
        self.sa = self.mapistore.QueryInterface(IID_IECServiceAdmin)
        self.ems = self.mapistore.QueryInterface(IID_IExchangeManageStore)
        self._ab = None
        self._admin_store = None
        self._gab = None
        entryid = HrGetOneProp(self.mapistore, PR_STORE_ENTRYID).Value
        self.pseudo_url = entryid[entryid.find(b'pseudo:'):-1] # XXX ECSERVER
        self.name = self.pseudo_url[9:] # XXX get this kind of stuff from pr_ec_statstable_servers..?

    def nodes(self): # XXX delay mapi sessions until actually needed
        for row in self.table(PR_EC_STATSTABLE_SERVERS).dict_rows():
            yield Server(options=self.options, config=self.config, sslkey_file=self.sslkey_file, sslkey_pass=self.sslkey_pass, server_socket=row[PR_EC_STATS_SERVER_HTTPSURL], log=self.log, service=self.service)

    def table(self, name, restriction=None, order=None, columns=None):
        return Table(self, self.mapistore.OpenProperty(name, IID_IMAPITable, MAPI_UNICODE, 0), name, restriction=restriction, order=order, columns=columns)

    def tables(self):
        for table in (PR_EC_STATSTABLE_SYSTEM, PR_EC_STATSTABLE_SESSIONS, PR_EC_STATSTABLE_USERS, PR_EC_STATSTABLE_COMPANY, PR_EC_STATSTABLE_SERVERS):
            try:
                yield self.table(table)
            except MAPIErrorNotFound:
                pass

    def gab_table(self): # XXX separate addressbook class? useful to add to self.tables?
        ct = self.gab.GetContentsTable(MAPI_DEFERRED_ERRORS)
        return Table(self, ct, PR_CONTAINER_CONTENTS)

    @property
    def ab(self):
        """ Address Book """
        if not self._ab:
            self._ab = self.mapisession.OpenAddressBook(0, None, 0) # XXX
        return self._ab

    @property
    def admin_store(self):
        if not self._admin_store:
            self._admin_store = Store(mapiobj=self.mapistore, server=self)
        return self._admin_store

    @property
    def gab(self):
        """ Global Address Book """

        if not self._gab:
            self._gab = self.ab.OpenEntry(self.ab.GetDefaultDir(), None, 0)
        return self._gab

    @property
    def guid(self):
        """ Server GUID """

        return bin2hex(HrGetOneProp(self.mapistore, PR_MAPPING_SIGNATURE).Value)

    def user(self, name=None, email=None, create=False):
        """ Return :class:`user <User>` with given name """

        try:
            return User(name, email=email, server=self)
        except NotFoundError:
            if create and name:
                return self.create_user(name)
            else:
                raise

    def get_user(self, name):
        """ Return :class:`user <User>` with given name or *None* if not found """

        try:
            return self.user(name)
        except Error:
            pass

    def users(self, remote=False, system=False, parse=True):
        """ Return all :class:`users <User>` on server

            :param remote: include users on remote server nodes
            :param system: include system users
        """

        if parse and getattr(self.options, 'users', None):
            for username in self.options.users:
                if '*' in username or '?' in username: # XXX unicode.. need to use something like boost::wregex in ZCP?
                    regex = username.replace('*', '.*').replace('?', '.')
                    restriction = SPropertyRestriction(RELOP_RE, PR_DISPLAY_NAME_A, SPropValue(PR_DISPLAY_NAME_A, regex))
                    for match in MAPI.Util.AddressBook.GetAbObjectList(self.mapisession, restriction):
                        if fnmatch.fnmatch(match, username):
                            yield User(match, self)
                else:
                    yield User(_decode(username), self) # XXX can optparse output unicode?
            return
        try:
            for name in self._companylist():
                for user in Company(name, self).users(): # XXX remote/system check
                    yield user
        except MAPIErrorNoSupport:
            for username in MAPI.Util.AddressBook.GetUserList(self.mapisession, None, MAPI_UNICODE):
                user = User(username, self)
                if system or username != u'SYSTEM':
                    if remote or user._ecuser.Servername in (self.name, ''):
                        yield user
                    # XXX following two lines not necessary with python-mapi from trunk
                    elif not remote and user.local: # XXX check if GetUserList can filter local/remote users
                        yield user

    def create_user(self, name, email=None, password=None, company=None, fullname=None, create_store=True):
        """ Create a new :class:`user <Users>` on the server

        :param name: the login name of the new user
        :param email: the email address of the user
        :param password: the login password of the user
        :param company: the company of the user
        :param fullname: the full name of the user
        :param create_store: should a store be created for the new user
        :return: :class:`<User>`
        """
        name = unicode(name)
        fullname = unicode(fullname or '')
        if email:
            email = unicode(email)
        else:
            email = u'%s@%s' % (name, socket.gethostname())
        if password:
            password = unicode(password)
        if company:
            company = unicode(company)
        if company and company != u'Default':
            usereid = self.sa.CreateUser(ECUSER(u'%s@%s' % (name, company), password, email, fullname), MAPI_UNICODE)
            user = self.company(company).user(u'%s@%s' % (name, company))
        else:
            usereid = self.sa.CreateUser(ECUSER(name, password, email, fullname), MAPI_UNICODE)
            user = self.user(name)
        if create_store:
            self.sa.CreateStore(ECSTORE_TYPE_PRIVATE, _unhex(user.userid))
        return user

    def remove_user(self, name): # XXX delete(object)?
        user = self.user(name)
        self.sa.DeleteUser(user._ecuser.UserID)

    def company(self, name, create=False):
        """ Return :class:`company <Company>` with given name; raise exception if not found """

        try:
            return Company(name, self)
        except NotFoundError:
            if create:
                return self.create_company(name)
            else:
                raise

    def get_company(self, name):
        """ Return :class:`company <Company>` with given name or *None* if not found """

        try:
            return self.company(name)
        except Error:
            pass

    def remove_company(self, name): # XXX delete(object)?
        company = self.company(name)
        self.sa.DeleteCompany(company._eccompany.CompanyID)

    def _companylist(self): # XXX fix self.sa.GetCompanyList(MAPI_UNICODE)? looks like it's not swigged correctly?
        self.sa.GetCompanyList(MAPI_UNICODE) # XXX exception for single-tenant....
        return MAPI.Util.AddressBook.GetCompanyList(self.mapisession, MAPI_UNICODE)

    @property
    def multitenant(self):
        """ Return boolean showing if the server is multitenant """

        try:
            self._companylist()
            return True
        except MAPIErrorNoSupport:
            return False

    def companies(self, remote=False, parse=True): # XXX remote?
        """ Return all :class:`companies <Company>` on server

            :param remote: include companies without users on this server node
        """
        if parse and getattr(self.options, 'companies', None):
            for name in self.options.companies:
                name = _decode(name) # can optparse give us unicode?
                try:
                    yield Company(name, self)
                except MAPIErrorNoSupport:
                    raise NotFoundError('no such company: %s' % name)
            return
        try:
            for name in self._companylist():
                yield Company(name, self)
        except MAPIErrorNoSupport:
            yield Company(u'Default', self)

    def create_company(self, name): # XXX deprecated because of company(create=True)?
        name = unicode(name)
        companyeid = self.sa.CreateCompany(ECCOMPANY(name, None), MAPI_UNICODE)
        return self.company(name)

    def _store(self, guid):
        if len(guid) != 32:
            raise Error("invalid store id: '%s'" % guid)
        try:
            storeid = _unhex(guid)
        except:
            raise Error("invalid store id: '%s'" % guid)
        table = self.ems.GetMailboxTable(None, 0) # XXX merge with Store.__init__
        table.SetColumns([PR_ENTRYID], 0)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_STORE_RECORD_KEY, SPropValue(PR_STORE_RECORD_KEY, storeid)), TBL_BATCH)
        for row in table.QueryRows(-1, 0):
            return self.mapisession.OpenMsgStore(0, row[0].Value, None, MDB_WRITE)
        raise NotFoundError("no such store: '%s'" % guid)

    def _store2(self, storeid): # XXX max lifetime
        if storeid not in self._store_cache:
            self._store_cache[storeid] = self.mapisession.OpenMsgStore(0, storeid, IID_IMsgStore, MDB_WRITE)
        return self._store_cache[storeid]

    def groups(self):
        """ Return all :class:`groups <Group>` on server """

        for name in MAPI.Util.AddressBook.GetGroupList(self.mapisession, None, MAPI_UNICODE):
            yield Group(name, self)

    def group(self, name):
        """ Return :class:`group <Group>` with given name """

        return Group(name, self)

    def create_group(self, name, fullname='', email='', hidden = False, groupid = None):
        name = unicode(name) # XXX: fullname/email unicode?
        email = unicode(email)
        fullname = unicode(fullname)
        companyeid = self.sa.CreateGroup(ECGROUP(name, fullname, email, int(hidden), groupid), MAPI_UNICODE)

        return self.group(name)

    def remove_group(self, name):
        group = self.group(name)
        self.sa.DeleteGroup(group._ecgroup.GroupID)

    def store(self, guid=None, entryid=None):
        """ Return :class:`store <Store>` with given GUID """

        if guid == 'public':
            if not self.public_store:
                raise NotFoundError("no public store")
            return self.public_store
        else:
            return Store(guid=guid, entryid=entryid, server=self)

    def get_store(self, guid):
        """ Return :class:`store <Store>` with given GUID or *None* if not found """

        try:
            return self.store(guid)
        except Error:
            pass

    def stores(self, system=False, remote=False, parse=True): # XXX implement remote
        """ Return all :class:`stores <Store>` on server node

        :param system: include system stores
        :param remote: include stores on other nodes

        """

        if parse and getattr(self.options, 'stores', None):
            for guid in self.options.stores:
                if guid == 'public': # XXX check self.options.companies?
                    if not self.public_store:
                        raise NotFoundError("no public store")
                    yield self.public_store
                else:
                    yield Store(guid, server=self)
            return

        table = self.ems.GetMailboxTable(None, 0)
        table.SetColumns([PR_DISPLAY_NAME_W, PR_ENTRYID], 0)
        for row in table.QueryRows(-1, 0):
            store = Store(mapiobj=self.mapisession.OpenMsgStore(0, row[1].Value, None, MDB_WRITE), server=self) # XXX cache
            if system or store.public or (store.user and store.user.name != 'SYSTEM'):
                yield store

    def create_store(self, public=False):
        if public:
            mapistore = self.sa.CreateStore(ECSTORE_TYPE_PUBLIC, EID_EVERYONE)
            return Store(mapiobj=mapistore, server=self)
        # XXX

    def remove_store(self, store): # XXX server.delete?
        self.sa.RemoveStore(_unhex(store.guid))

    def sync_users(self):
        # Flush user cache on the server
        self.sa.SyncUsers(None)

    @property
    def public_store(self):
        """ public :class:`store <Store>` in single-company mode """

        try:
            self.sa.GetCompanyList(MAPI_UNICODE)
            raise Error('request for server-wide public store in multi-company setup')
        except MAPIErrorNoSupport:
            return next(self.companies()).public_store

    @property
    def state(self):
        """ Current server state """

        return _state(self.mapistore)

    def sync(self, importer, state, log=None, max_changes=None, window=None, begin=None, end=None, stats=None):
        """ Perform synchronization against server node

        :param importer: importer instance with callbacks to process changes
        :param state: start from this state (has to be given)
        :log: logger instance to receive important warnings/errors
        """

        importer.store = None
        return _sync(self, self.mapistore, importer, state, log or self.log, max_changes, window=window, begin=begin, end=end, stats=stats)

    @_timed_cache(minutes=60)
    def _resolve_email(self, entryid=None):
        try:
            mailuser = self.mapisession.OpenEntry(entryid, None, 0)
            return self.user(HrGetOneProp(mailuser, PR_ACCOUNT_W).Value).email # XXX PR_SMTP_ADDRESS_W from mailuser?
        except (Error, MAPIErrorNotFound): # XXX deleted user
            return '' # XXX groups

    def __str__(self):
        return u'Server(%s)' % self.server_socket

    def __repr__(self):
        return _repr(self)

# interactive shortcuts
def user(name):
    return Server().user(name)

def users(*args, **kwargs):
    return Server().users(*args, **kwargs)

def store(guid):
    return Server().store(guid)

def stores(*args, **kwargs):
    return Server().stores(*args, **kwargs)

def companies(*args, **kwargs):
    return Server().companies(*args, **kwargs)

class Group(object):
    """Group class"""

    def __init__(self, name, server=None):
        self.server = server or Server()
        self._name = unicode(name)
        try:
            self._ecgroup = self.server.sa.GetGroup(self.server.sa.ResolveGroupName(self._name, MAPI_UNICODE), MAPI_UNICODE)
        except (MAPIErrorNotFound, MAPIErrorInvalidParameter):
            raise NotFoundError("no such group '%s'" % name)

    @property
    def groupid(self):
        return bin2hex(self._ecgroup.GroupID)

    def users(self):
        """Users in group"""

        return self.members(groups=False)

    def groups(self):
        """Groups in group"""

        return self.members(users=False)

    def members(self, groups=True, users=True):
        """All members in group, users or groups"""

        for ecuser in self.server.sa.GetUserListOfGroup(self._ecgroup.GroupID, MAPI_UNICODE):
            if ecuser.Username == 'SYSTEM':
                continue
            if users:
                try:
                    yield User(ecuser.Username, self.server)
                except NotFoundError: # XXX everyone, groups are included as users..
                    pass
            if groups:
                try:
                    yield Group(ecuser.Username, self.server)
                except NotFoundError:
                    pass

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, value):
        self._update(name=unicode(value))

    @property
    def email(self):
        return self._ecgroup.Email

    @email.setter
    def email(self, value):
        self._update(email=unicode(value))

    @property
    def fullname(self):
        return self._ecgroup.Fullname

    @fullname.setter
    def fullname(self, value):
        self._update(fullname=unicode(value))

    @property
    def hidden(self):
        return self._ecgroup.IsHidden == True

    @hidden.setter
    def hidden(self, value):
        self._update(hidden=value)

    # XXX: also does groups..
    def add_user(self, user):
        if isinstance(user, Group):
            self.server.sa.AddGroupUser(self._ecgroup.GroupID, user._ecgroup.GroupID)
        else:
            self.server.sa.AddGroupUser(self._ecgroup.GroupID, user._ecuser.UserID)

    def remove_user(self, user):
        self.server.sa.DeleteGroupUser(self._ecgroup.GroupID, user._ecuser.UserID)

    def _update(self, **kwargs):
        # XXX: crashes server on certain characters...
        self._name = kwargs.get('name', self.name)
        fullname = kwargs.get('fullname', self.fullname)
        email = kwargs.get('email', self.email)
        hidden = kwargs.get('hidden', self.hidden)
        group = ECGROUP(self._name, fullname, email, int(hidden), self._ecgroup.GroupID)
        self.server.sa.SetGroup(group, MAPI_UNICODE)
        self._ecgroup = self.server.sa.GetGroup(self.server.sa.ResolveGroupName(self._name, MAPI_UNICODE), MAPI_UNICODE)

    def __eq__(self, u): # XXX check same server?
        if isinstance(u, Group):
            return self.groupid == u.groupid
        return False

    def __ne__(self, g):
        return not self == g

    def __contains__(self, u): # XXX subgroups
        return u in self.users()

    def __unicode__(self):
        return u"Group('%s')" % self.name

    def __repr__(self):
        return _repr(self)


class Company(object):
    """Company class"""

    def __init__(self, name, server=None):
        self._name = name = unicode(name)
        self.server = server or Server()
        if name != u'Default': # XXX
            try:
                self._eccompany = self.server.sa.GetCompany(self.server.sa.ResolveCompanyName(self._name, MAPI_UNICODE), MAPI_UNICODE)
            except MAPIErrorNotFound:
                raise NotFoundError("no such company: '%s'" % name)

    @property
    def companyid(self): # XXX single-tenant case
        return bin2hex(self._eccompany.CompanyID)

    @property
    def name(self):
        """ Company name """

        return self._name

    def store(self, guid):
        if guid == 'public':
            if not self.public_store:
                raise NotFoundError("no public store for company '%s'" % self.name)
            return self.public_store
        else:
            return self.server.store(guid)

    def stores(self):
        if self.server.multitenant:
            table = self.server.sa.OpenUserStoresTable(MAPI_UNICODE)
            table.Restrict(SPropertyRestriction(RELOP_EQ, PR_EC_COMPANY_NAME_W, SPropValue(PR_EC_COMPANY_NAME_W, self.name)), TBL_BATCH)
            for row in table.QueryRows(-1,0):
                prop = PpropFindProp(row, PR_EC_STOREGUID)
                if prop:
                    yield Store(codecs.encode(prop.Value, 'hex'), self.server)
        else:
            for store in self.server.stores():
                yield store

    @property
    def public_store(self):
        """ Company public :class:`store <Store>` """

        if self._name == u'Default': # XXX 
            pubstore = GetPublicStore(self.server.mapisession)
            if pubstore is None:
                return None
            return Store(mapiobj=pubstore, server=self.server)
        try:
            entryid = self.server.ems.CreateStoreEntryID(None, self._name, MAPI_UNICODE)
        except MAPIErrorNotFound:
            return None
        return Store(entryid=entryid, server=self.server)

    def create_store(self, public=False):
        if public:
            if self._name == u'Default':
                mapistore = self.server.sa.CreateStore(ECSTORE_TYPE_PUBLIC, EID_EVERYONE)
            else:
                mapistore = self.server.sa.CreateStore(ECSTORE_TYPE_PUBLIC, self._eccompany.CompanyID)
            return Store(mapiobj=mapistore, server=self.server)
        # XXX

    def user(self, name, create=False):
        """ Return :class:`user <User>` with given name; raise exception if not found """

        name = unicode(name)
        for user in self.users(): # XXX slow
            if user.name == name:
                return User(name, self.server)
        if create:
            return self.create_user(name)
        else:
            raise NotFoundError("no such user: '%s'" % name)

    def get_user(self, name):
        """ Return :class:`user <User>` with given name or *None* if not found """

        try:
            return self.user(name)
        except Error:
            pass

    def users(self, parse=True):
        """ Return all :class:`users <User>` within company """

        if parse and getattr(self.server.options, 'users', None):
            for username in self.server.options.users:
                yield User(username, self.server)
            return

        for username in MAPI.Util.AddressBook.GetUserList(self.server.mapisession, self._name if self._name != u'Default' else None, MAPI_UNICODE): # XXX serviceadmin?
            if username != 'SYSTEM':
                yield User(username, self.server)

    def create_user(self, name, password=None):
        self.server.create_user(name, password=password, company=self._name)
        return self.user('%s@%s' % (name, self._name))

    def groups(self):
        if self.name == u'Default': # XXX
            for ecgroup in self.server.sa.GetGroupList(None, MAPI_UNICODE):
                yield Group(ecgroup.Groupname, self.server)
        else:
            for ecgroup in self.server.sa.GetGroupList(self._eccompany.CompanyID, MAPI_UNICODE):
                yield Group(ecgroup.Groupname, self.server)

    @property
    def quota(self):
        """ Company :class:`Quota` """

        if self._name == u'Default':
            return Quota(self.server, None)
        else:
            return Quota(self.server, self._eccompany.CompanyID)

    def __eq__(self, c):
        if isinstance(c, Company):
            return self.companyid == c.companyid
        return False

    def __ne__(self, c):
        return not self == c

    def __contains__(self, u):
        return u in self.users()

    def __unicode__(self):
        return u"Company('%s')" % self._name

    def __repr__(self):
        return _repr(self)

class Store(object):
    """Store class"""

    def __init__(self, guid=None, entryid=None, mapiobj=None, server=None):
        self.server = server or Server()
        if guid:
            mapiobj = self.server._store(guid)
        elif entryid:
            mapiobj = self.server._store2(entryid)
        self.mapiobj = mapiobj
        # XXX: fails if store is orphaned and guid is given..
        self._root = self.mapiobj.OpenEntry(None, None, 0)

    @property
    def entryid(self):
        """ Store entryid """

        return bin2hex(self.prop(PR_ENTRYID).value)

    @property
    def public(self):
        return self.prop(PR_MDB_PROVIDER).mapiobj.Value == ZARAFA_STORE_PUBLIC_GUID

    @property
    def guid(self):
        """ Store GUID """

        return bin2hex(self.prop(PR_STORE_RECORD_KEY).value)

    @property
    def hierarchyid(self):
        return  self.prop(PR_EC_HIERARCHYID).value

    @property
    def root(self):
        """ :class:`Folder` designated as store root """

        return Folder(self, HrGetOneProp(self._root, PR_ENTRYID).Value)

    @property
    def subtree(self):
        """ :class:`Folder` designated as IPM.Subtree """

        try:
            if self.public:
                ipmsubtreeid = HrGetOneProp(self.mapiobj, PR_IPM_PUBLIC_FOLDERS_ENTRYID).Value
            else:
                ipmsubtreeid = HrGetOneProp(self.mapiobj, PR_IPM_SUBTREE_ENTRYID).Value

            return Folder(self, ipmsubtreeid)
        except MAPIErrorNotFound:
            pass

    @property
    def findroot(self):
        """ :class:`Folder` designated as search-results root """

        try:
            return Folder(self, HrGetOneProp(self.mapiobj, PR_FINDER_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def inbox(self):
        """ :class:`Folder` designated as inbox """

        try:
            return Folder(self, self.mapiobj.GetReceiveFolder(u'IPM', MAPI_UNICODE)[0])
        except MAPIErrorNotFound:
            pass

    @property
    def junk(self):
        """ :class:`Folder` designated as junk """

        # PR_ADDITIONAL_REN_ENTRYIDS is a multi-value property, 4th entry is the junk folder
        try:
            return Folder(self, HrGetOneProp(self._root, PR_ADDITIONAL_REN_ENTRYIDS).Value[4])
        except MAPIErrorNotFound:
            pass

    @property
    def calendar(self):
        """ :class:`Folder` designated as calendar """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_APPOINTMENT_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def outbox(self):
        """ :class:`Folder` designated as outbox """

        try:
            return Folder(self, HrGetOneProp(self.mapiobj, PR_IPM_OUTBOX_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def contacts(self):
        """ :class:`Folder` designated as contacts """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_CONTACT_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def common_views(self):
        """ :class:`Folder` contains folders for managing views for the message store """

        try:
            return Folder(self, self.prop(PR_COMMON_VIEWS_ENTRYID).value)
        except MAPIErrorNotFound:
            pass

    @property
    def drafts(self):
        """ :class:`Folder` designated as drafts """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_DRAFTS_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def wastebasket(self):
        """ :class:`Folder` designated as wastebasket """

        try:
            return Folder(self, HrGetOneProp(self.mapiobj, PR_IPM_WASTEBASKET_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def journal(self):
        """ :class:`Folder` designated as journal """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_JOURNAL_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def notes(self):
        """ :class:`Folder` designated as notes """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_NOTE_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def sentmail(self):
        """ :class:`Folder` designated as sentmail """

        try:
            return Folder(self, HrGetOneProp(self.mapiobj, PR_IPM_SENTMAIL_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def tasks(self):
        """ :class:`Folder` designated as tasks """

        try:
            return Folder(self, HrGetOneProp(self._root, PR_IPM_TASK_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def suggested_contacts(self):
        """ :class`Folder` designated as Suggested contacts"""

        try:
            entryid = _extract_ipm_ol2007_entryids(self.inbox.prop(PR_IPM_OL2007_ENTRYIDS).value, RSF_PID_SUGGESTED_CONTACTS)
            return Folder(self, _unhex(entryid))
        except MAPIErrorNotFound:
            pass

    @property
    def rss(self):
        """ :class`Folder` designated as RSS items"""

        try:
            entryid = _extract_ipm_ol2007_entryids(self.inbox.prop(PR_IPM_OL2007_ENTRYIDS).value, RSF_PID_RSS_SUBSCRIPTION)
            return Folder(self, _unhex(entryid))
        except MAPIErrorNotFound:
            pass

    def delete(self, props):
        """Delete properties from an Store

        :param props: The properties to remove
        """

        if isinstance(props, Property):
            props = [props]

        self.mapiobj.DeleteProps([p.proptag for p in props])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def folder(self, path=None, entryid=None, recurse=False, create=False): # XXX sloowowowww
        """ Return :class:`Folder` with given path or entryid; raise exception if not found

        """

        if entryid is not None:
            try:
                return Folder(self, _unhex(entryid))
            except (MAPIErrorInvalidEntryid, MAPIErrorNotFound):
                raise NotFoundError("no folder with entryid: '%s'" % entryid)

        return self.subtree.folder(path, recurse=recurse, create=create)

    def get_folder(self, path=None, entryid=None):
        """ Return :class:`folder <Folder>` with given name/entryid or *None* if not found """

        try:
            return self.folder(path, entryid=entryid)
        except NotFoundError:
            pass

    def folders(self, recurse=True, parse=True):
        """ Return all :class:`folders <Folder>` in store

        :param recurse: include all sub-folders
        :param system: include system folders

        """

        # parse=True
        filter_names = None
        if parse and getattr(self.server.options, 'folders', None):
            for path in self.server.options.folders:
                yield self.folder(_decode(path)) # XXX can optparse output unicode?
            return

        if self.subtree:
            for folder in self.subtree.folders(recurse=recurse):
                yield folder

    def item(self, entryid):
        """ Return :class:`Item` with given entryid; raise exception of not found """ # XXX better exception?

        item = Item() # XXX copy-pasting..
        item.store = self
        item.server = self.server
        item.mapiobj = _openentry_raw(self.mapiobj, _unhex(entryid), MAPI_MODIFY) # XXX soft-deleted item?
        return item

    @property
    def size(self):
        """ Store size """

        return self.prop(PR_MESSAGE_SIZE_EXTENDED).value

    def config_item(self, name):
        item = Item()
        item.mapiobj = libcommon.GetConfigMessage(self.mapiobj, name)
        return item

    @property
    def last_logon(self):
        """ Return :datetime Last logon of a user on this store """

        return self.prop(PR_LAST_LOGON_TIME).value or None

    @property
    def last_logoff(self):
        """ Return :datetime of the last logoff of a user on this store """

        return self.prop(PR_LAST_LOGOFF_TIME).value or None

    @property
    def outofoffice(self):
        """ Return :class:`Outofoffice` """

        # FIXME: If store is public store, return None?
        return Outofoffice(self)

    @property
    def user(self):
        """ Store :class:`owner <User>` """

        try:
            userid = HrGetOneProp(self.mapiobj, PR_MAILBOX_OWNER_ENTRYID).Value # XXX
            return User(self.server.sa.GetUser(userid, MAPI_UNICODE).Username, self.server)
        except MAPIErrorNotFound:
            pass

    @property
    def archive_store(self):
        """ Archive :class:`Store` or *None* if not found """

        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
            entryid = HrGetOneProp(self.mapiobj, PROP_STORE_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        return Store(entryid=entryid, server=self.server) # XXX server?

    @archive_store.setter
    def archive_store(self, store):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        # XXX only for detaching atm
        if store is None:
            self.mapiobj.DeleteProps([PROP_STORE_ENTRYIDS, PROP_ITEM_ENTRYIDS])
            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def archive_folder(self):
        """ Archive :class:`Folder` or *None* if not found """

        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
            arch_folderid = HrGetOneProp(self.mapiobj, PROP_ITEM_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        return self.archive_store.folder(entryid=codecs.encode(arch_folderid, 'hex'))

    @property
    def company(self):
        table = self.server.sa.OpenUserStoresTable(MAPI_UNICODE)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_EC_STOREGUID, SPropValue(PR_EC_STOREGUID, _unhex(self.guid))), TBL_BATCH)
        for row in table.QueryRows(1,0):
            companyname = PpropFindProp(row, PR_EC_COMPANY_NAME_W)
            if companyname is None: # XXX single-tenant, improve check..
                return next(self.server.companies())
            else:
                return self.server.company(companyname.Value)

    @property
    def orphan(self):
        if self.public:
            pubstore = self.company.public_store
            return (pubstore is None or pubstore.guid != self.guid)
        else:
            return (self.user.store is None or self.user.store.guid != self.guid)

    def create_prop(self, proptag, value, proptype=None):
        return _create_prop(self, self.mapiobj, proptag, value, proptype)

    def prop(self, proptag):
        return _prop(self, self.mapiobj, proptag)

    def props(self, namespace=None):
        return _props(self.mapiobj, namespace=namespace)

    def create_searchfolder(self, text=None): # XXX store.findroot.create_folder()?
        import uuid # XXX username+counter? permission problems to determine number?
        mapiobj = self.findroot.mapiobj.CreateFolder(FOLDER_SEARCH, str(uuid.uuid4()), 'comment', None, 0)
        return Folder(self, mapiobj=mapiobj)

    def permissions(self):
        return _permissions(self)

    def permission(self, member, create=False):
        return _permission(self, member, create)

    def favorites(self):
        """Returns all favorite folders"""

        table = self.common_views.mapiobj.GetContentsTable(MAPI_ASSOCIATED)
        table.SetColumns([PR_MESSAGE_CLASS, PR_SUBJECT, PR_WLINK_ENTRYID, PR_WLINK_FLAGS, PR_WLINK_ORDINAL, PR_WLINK_STORE_ENTRYID, PR_WLINK_TYPE], 0)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, SPropValue(PR_MESSAGE_CLASS, "IPM.Microsoft.WunderBar.Link")), TBL_BATCH)

        for row in table.QueryRows(-1, 0):
            entryid = bin2hex(row[2].Value)
            store_entryid = bin2hex(row[5].Value)

            if store_entryid == self.entryid: # XXX: Handle favorites from public stores
                try:
                    yield self.folder(entryid=bin2hex(row[2].Value))
                except NotFoundError:
                    pass

    def _subprops(self, value):
        result = {}
        pos = 0
        while pos < len(value):
            id_ = ord(value[pos])
            cb = ord(value[pos+1])
            result[id_] = value[pos+2:pos+2+cb]
            pos += 2 + cb
        return result

    def searches(self):
        """Returns all permanent search folders """

        findroot = self.root.folder('FINDER_ROOT') # XXX

        # extract special type of guid from search folder PR_FOLDER_DISPLAY_FLAGS to match against
        guid_folder = {}
        for folder in findroot.folders():
            try:
                prop = folder.prop(PR_FOLDER_DISPLAY_FLAGS)
                subprops = self._subprops(prop.value)
                guid_folder[subprops[2]] = folder
            except MAPIErrorNotFound:
                pass

        # match common_views SFInfo records against these guids
        table = self.common_views.mapiobj.GetContentsTable(MAPI_ASSOCIATED)
        table.SetColumns([PR_MESSAGE_CLASS, PR_WB_SF_ID], MAPI_UNICODE)
        table.Restrict(SPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, SPropValue(PR_MESSAGE_CLASS, "IPM.Microsoft.WunderBar.SFInfo")), TBL_BATCH)

        for row in table.QueryRows(-1, 0):
            try:
                yield guid_folder[row[1].Value]
            except KeyError :
                pass

    def __eq__(self, s): # XXX check same server?
        if isinstance(s, Store):
            return self.guid == s.guid
        return False

    def __ne__(self, s):
        return not self == s

    def __unicode__(self):
        return u"Store('%s')" % self.guid

    def __repr__(self):
        return _repr(self)

class Folder(object):
    """Folder class"""

    def __init__(self, store, entryid=None, associated=False, deleted=False, mapiobj=None): # XXX entryid not hex-encoded!?
        self.store = store
        self.server = store.server
        if mapiobj:
            self.mapiobj = mapiobj
            self._entryid = HrGetOneProp(self.mapiobj, PR_ENTRYID).Value
        else:
            self._entryid = entryid
            try:
                self.mapiobj = store.mapiobj.OpenEntry(entryid, IID_IMAPIFolder, MAPI_MODIFY)
            except MAPIErrorNoAccess: # XXX XXX
                self.mapiobj = store.mapiobj.OpenEntry(entryid, IID_IMAPIFolder, 0)
        self.content_flag = MAPI_ASSOCIATED if associated else (SHOW_SOFT_DELETES if deleted else 0) 

    @property
    def entryid(self):
        """ Folder entryid """

        return bin2hex(self._entryid)

    @property
    def sourcekey(self):
        return bin2hex(HrGetOneProp(self.mapiobj, PR_SOURCE_KEY).Value)

    @property
    def parent(self):
        """Return :class:`parent <Folder>` or None"""

        if self.entryid != self.store.root.entryid:
            try:
                return Folder(self.store, self.prop(PR_PARENT_ENTRYID).value)
            except MAPIErrorNotFound: # XXX: Should not happen
                pass

    @property
    def hierarchyid(self):
        return self.prop(PR_EC_HIERARCHYID).value

    @property
    def folderid(self): # XXX remove?
        return self.hierarchyid

    @property
    def subfolder_count(self):
        """ Number of direct subfolders """

        return self.prop(PR_FOLDER_CHILD_COUNT).value

    @property
    def name(self):
        """ Folder name """

        try:
            return self.prop(PR_DISPLAY_NAME_W).value.replace('/', '\\/')
        except MAPIErrorNotFound:
            if self.entryid == self.store.root.entryid: # Root folder's PR_DISPLAY_NAME_W is never set
                return u'ROOT'
            else:
                return u''

    @property
    def path(self):
        names = []
        parent = self
        while parent and not parent.entryid == self.store.subtree.entryid:
            names.append(parent.name)
            parent = parent.parent
        return '/'.join(reversed(names))

    @name.setter
    def name(self, name):
        self.mapiobj.SetProps([SPropValue(PR_DISPLAY_NAME_W, unicode(name))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def container_class(self):
        """
        Property which describes the type of items a folder holds, possible values
        * IPF.Appointment
        * IPF.Contact
        * IPF.Journal
        * IPF.Note
        * IPF.StickyNote
        * IPF.Task

        https://msdn.microsoft.com/en-us/library/aa125193(v=exchg.65).aspx
        """

        try:
            return self.prop(PR_CONTAINER_CLASS_W).value
        except MAPIErrorNotFound:
            pass

    @container_class.setter
    def container_class(self, value):
        self.mapiobj.SetProps([SPropValue(PR_CONTAINER_CLASS_W, unicode(value))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def unread(self):
        """ Number of unread items """

        return self.prop(PR_CONTENT_UNREAD).value

    def item(self, entryid):
        """ Return :class:`Item` with given entryid; raise exception of not found """ # XXX better exception?

        item = Item() # XXX copy-pasting..
        item.store = self.store
        item.server = self.server
        item.mapiobj = _openentry_raw(self.store.mapiobj, _unhex(entryid), MAPI_MODIFY | self.content_flag)
        return item

    def items(self):
        """ Return all :class:`items <Item>` in folder, reverse sorted on received date """

        try:
            table = self.mapiobj.GetContentsTable(self.content_flag)
        except MAPIErrorNoSupport:
            return

        table.SortTable(SSortOrderSet([SSort(PR_MESSAGE_DELIVERY_TIME, TABLE_SORT_DESCEND)], 0, 0), 0) # XXX configure
        while True:
            rows = table.QueryRows(50, 0)
            if len(rows) == 0:
                break
            for row in rows:
                item = Item()
                item.store = self.store
                item.server = self.server
                item.mapiobj = _openentry_raw(self.store.mapiobj, PpropFindProp(row, PR_ENTRYID).Value, MAPI_MODIFY | self.content_flag)
                yield item

    def occurrences(self, start=None, end=None):
        if start and end:
            startstamp = time.mktime(start.timetuple())
            endstamp = time.mktime(end.timetuple())

            # XXX use shortcuts and default type (database) to avoid MAPI snake wrestling
            NAMED_PROPS = [MAPINAMEID(PSETID_Appointment, MNID_ID, x) for x in (33293, 33294, 33315)]
            ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS, 0)
            duedate = ids[0] | PT_SYSTIME
            startdate = ids[1] | PT_SYSTIME
            recurring = ids[2] | PT_BOOLEAN

            # only look at non-recurring items which overlap and all recurring items
            restriction = SOrRestriction([
                SOrRestriction([
                    SAndRestriction([
                        SPropertyRestriction(RELOP_GT, duedate, SPropValue(duedate, MAPI.Time.unixtime(startstamp))),
                        SPropertyRestriction(RELOP_LT, startdate, SPropValue(startdate, MAPI.Time.unixtime(endstamp))),
                    ]),
                    SAndRestriction([
                        SPropertyRestriction(RELOP_EQ, startdate, SPropValue(startdate, MAPI.Time.unixtime(startstamp))),
                        SPropertyRestriction(RELOP_EQ, duedate, SPropValue(duedate, MAPI.Time.unixtime(startstamp))),
                    ]),
                ]),
                SAndRestriction([
                    SPropertyRestriction(RELOP_EQ, recurring, SPropValue(recurring, True))
                ])
            ])

            table = self.mapiobj.GetContentsTable(0)
            table.SetColumns([PR_ENTRYID], 0)
            table.Restrict(restriction, 0)
            rows = table.QueryRows(-1, 0)
            for row in rows:
                entryid = codecs.encode(row[0].Value, 'hex')
                for occurrence in self.item(entryid).occurrences(start, end):
                    yield occurrence

        else:
            for item in self:
                for occurrence in item.occurrences(start, end):
                    yield occurrence

    def create_item(self, eml=None, ics=None, vcf=None, load=None, loads=None, attachments=True, **kwargs): # XXX associated
        item = Item(self, eml=eml, ics=ics, vcf=vcf, load=load, loads=loads, attachments=attachments, create=True)
        for key, val in kwargs.items():
            setattr(item, key, val)
        return item

    # XXX: always hard delete or but we should also provide 'softdelete' which moves the item to the wastebasket
    def empty(self, recurse=True, associated=False):
        """ Delete folder contents

        :param recurse: delete subfolders
        :param associated: delete associated contents
        """

        if recurse:
            flags = DELETE_HARD_DELETE
            if associated:
                flags |= DEL_ASSOCIATED
            self.mapiobj.EmptyFolder(0, None, flags)
        else:
            self.delete(self.items()) # XXX look at associated flag! probably also quite slow

    @property
    def size(self): # XXX bit slow perhaps? :P
        """ Folder size """

        try:
            table = self.mapiobj.GetContentsTable(self.content_flag)
        except MAPIErrorNoSupport:
            return 0

        table.SetColumns([PR_MESSAGE_SIZE], 0)
        table.SeekRow(BOOKMARK_BEGINNING, 0)
        rows = table.QueryRows(-1, 0)
        size = 0
        for row in rows:
            size += row[0].Value
        return size

    @property
    def count(self, recurse=False): # XXX implement recurse?
        """ Number of items in folder

        :param recurse: include items in sub-folders

        """

        try:
            return self.mapiobj.GetContentsTable(self.content_flag).GetRowCount(0) # XXX PR_CONTENT_COUNT, PR_ASSOCIATED_CONTENT_COUNT, PR_CONTENT_UNREAD?
        except MAPIErrorNoSupport:
            return 0

    def _get_entryids(self, items):
        if isinstance(items, (Item, Folder, Permission)):
            items = [items]
        else:
            items = list(items)
        item_entryids = [_unhex(item.entryid) for item in items if isinstance(item, Item)]
        folder_entryids = [_unhex(item.entryid) for item in items if isinstance(item, Folder)]
        perms = [item for item in items if isinstance(item, Permission)]
        return item_entryids, folder_entryids, perms

    def delete(self, items): # XXX associated
        item_entryids, folder_entryids, perms = self._get_entryids(items)
        if item_entryids:
            self.mapiobj.DeleteMessages(item_entryids, 0, None, DELETE_HARD_DELETE)
        for entryid in folder_entryids:
            self.mapiobj.DeleteFolder(entryid, 0, None, DEL_FOLDERS|DEL_MESSAGES)
        for perm in perms:
            acl_table = self.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
            acl_table.ModifyTable(0, [ROWENTRY(ROW_REMOVE, [SPropValue(PR_MEMBER_ID, perm.mapirow[PR_MEMBER_ID])])])

    def copy(self, items, folder, _delete=False):
        item_entryids, folder_entryids, perms = self._get_entryids(items) # XXX copy/move perms??
        if item_entryids:
            self.mapiobj.CopyMessages(item_entryids, IID_IMAPIFolder, folder.mapiobj, 0, None, (MESSAGE_MOVE if _delete else 0))
        for entryid in folder_entryids:
            self.mapiobj.CopyFolder(entryid, IID_IMAPIFolder, folder.mapiobj, None, 0, None, (FOLDER_MOVE if _delete else 0))

    def move(self, items, folder):
        self.copy(items, folder, _delete=True)

    def folder(self, path=None, entryid=None, recurse=False, create=False): # XXX kill (slow) recursive search
        """ Return :class:`Folder` with given path or entryid; raise exception if not found

            :param key: name, path or entryid
        """

        if entryid is not None:
            try:
                return Folder(self, _unhex(entryid))
            except (MAPIErrorInvalidEntryid, MAPIErrorNotFound, TypeError):
                raise NotFoundError

        if '/' in path.replace('\\/', ''): # XXX MAPI folders may contain '/' (and '\') in their names..
            subfolder = self
            for name in UNESCAPED_SLASH_RE.split(path):
                subfolder = subfolder.folder(name, create=create, recurse=False)
            return subfolder

        if self == self.store.subtree and path in ENGLISH_FOLDER_MAP: # XXX depth==0?
            path = getattr(self.store, ENGLISH_FOLDER_MAP[path]).name

        matches = [f for f in self.folders(recurse=recurse) if f.name == path]
        if len(matches) == 0:
            if create:
                name = path.replace('\\/', '/')
                mapifolder = self.mapiobj.CreateFolder(FOLDER_GENERIC, unicode(name), u'', None, MAPI_UNICODE)
                return Folder(self.store, HrGetOneProp(mapifolder, PR_ENTRYID).Value)
            else:
                raise NotFoundError("no such folder: '%s'" % path)
        elif len(matches) > 1:
            raise NotFoundError("multiple folders with name '%s'" % path)
        else:
            return matches[0]

    def get_folder(self, path=None, entryid=None):
        """ Return :class:`folder <Folder>` with given name/entryid or *None* if not found """

        try:
            return self.folder(path, entryid=entryid)
        except Error:
            pass

    def folders(self, recurse=True, depth=0):
        """ Return all :class:`sub-folders <Folder>` in folder

        :param recurse: include all sub-folders
        """

        try:
            table = self.mapiobj.GetHierarchyTable(MAPI_UNICODE)
        except MAPIErrorNoSupport: # XXX webapp search folder?
            return

        table.SetColumns([PR_ENTRYID], 0)
        rows = table.QueryRows(-1, 0)
        for row in rows:
            subfolder = self.mapiobj.OpenEntry(row[0].Value, None, MAPI_MODIFY)
            entryid = subfolder.GetProps([PR_ENTRYID], MAPI_UNICODE)[0].Value
            folder = Folder(self.store, entryid)
            folder.depth = depth
            yield folder
            if recurse:
                for subfolder in folder.folders(depth=depth+1):
                    yield subfolder

    def create_folder(self, path, **kwargs):
        folder = self.folder(path, create=True)
        for key, val in kwargs.items():
            setattr(folder, key, val)
        return folder

    def rules(self):
        rule_table = self.mapiobj.OpenProperty(PR_RULES_TABLE, IID_IExchangeModifyTable, 0, 0)
        table = Table(self.server, rule_table.GetTable(0), PR_RULES_TABLE)
        for row in table.dict_rows():
            yield Rule(row)

    def prop(self, proptag):
        return _prop(self, self.mapiobj, proptag)

    def create_prop(self, proptag, value, proptype=None):
        return _create_prop(self, self.mapiobj, proptag, value, proptype)

    def props(self):
        return _props(self.mapiobj)

    def table(self, name, restriction=None, order=None, columns=None): # XXX associated, PR_CONTAINER_CONTENTS?
        return Table(self.server, self.mapiobj.OpenProperty(name, IID_IMAPITable, MAPI_UNICODE, 0), name, restriction=restriction, order=order, columns=columns)

    def tables(self): # XXX associated, rules
        yield self.table(PR_CONTAINER_CONTENTS)
        yield self.table(PR_FOLDER_ASSOCIATED_CONTENTS)
        yield self.table(PR_CONTAINER_HIERARCHY)

    @property
    def state(self):
        """ Current folder state """

        return _state(self.mapiobj, self.content_flag == MAPI_ASSOCIATED)

    def sync(self, importer, state=None, log=None, max_changes=None, associated=False, window=None, begin=None, end=None, stats=None):
        """ Perform synchronization against folder

        :param importer: importer instance with callbacks to process changes
        :param state: start from this state; if not given sync from scratch
        :log: logger instance to receive important warnings/errors
        """

        if state is None:
            state = codecs.encode(8*b'\0', 'hex').upper()
        importer.store = self.store
        return _sync(self.store.server, self.mapiobj, importer, state, log, max_changes, associated, window=window, begin=begin, end=end, stats=stats)

    def readmbox(self, location):
        for message in mailbox.mbox(location):
            newitem = Item(self, eml=message.__str__(), create=True)

    def mbox(self, location): # FIXME: inconsistent with maildir()
        mboxfile = mailbox.mbox(location)
        mboxfile.lock()
        for item in self.items():
            mboxfile.add(item.eml())
        mboxfile.unlock()

    def maildir(self, location='.'):
        destination = mailbox.MH(location + '/' + self.name)
        destination.lock()
        for item in self.items():
            destination.add(item.eml())
        destination.unlock()

    def read_maildir(self, location):
        for message in mailbox.MH(location):
            newitem = Item(self, eml=message.__str__(), create=True)

    @property
    def associated(self):
        """ Associated folder containing hidden items """

        return Folder(self.store, self._entryid, associated=True)

    @property
    def deleted(self):
        return Folder(self.store, self._entryid, deleted=True)

    def permissions(self):
        return _permissions(self)

    def permission(self, member, create=False):
        return _permission(self, member, create)

    def rights(self, member):
        if member == self.store.user: # XXX admin-over-user, Store.rights (inheritance)
            return NAME_RIGHT.keys()
        parent = self
        feids = set() # avoid loops
        while parent.entryid not in feids:
            try:
                return parent.permission(member).rights
            except NotFoundError:
                if isinstance(member, User):
                    for group in member.groups():
                        try:
                            return parent.permission(group).rights
                        except NotFoundError:
                            pass
                    # XXX company
            feids.add(parent.entryid)
            parent = parent.parent
        return []

    def search(self, text): # XXX recursion
        searchfolder = self.store.create_searchfolder()
        searchfolder.search_start(self, text)
        searchfolder.search_wait()
        for item in searchfolder:
            yield item
        self.store.findroot.mapiobj.DeleteFolder(searchfolder.entryid.decode('hex'), 0, None, 0) # XXX store.findroot

    def search_start(self, folders, text): # XXX RECURSIVE_SEARCH
        # specific restriction format, needed to reach indexer
        restriction = SOrRestriction([
                        SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT_W, SPropValue(PR_SUBJECT_W, unicode(text))),
                        SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_BODY_W, SPropValue(PR_BODY_W, unicode(text))),
                        SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_DISPLAY_TO_W, SPropValue(PR_DISPLAY_TO_W, unicode(text))),
                        # XXX add all default fields.. BUT perform full-text search by default!
        ])
        if isinstance(folders, Folder):
            folders = [folders]
        self.mapiobj.SetSearchCriteria(restriction, [_unhex(f.entryid) for f in folders], 0)

    def search_wait(self):
        while True:
            (restrict, list, state) = self.mapiobj.GetSearchCriteria(0)
            if not state & SEARCH_REBUILD:
                break

    @property
    def archive_folder(self):
        """ Archive :class:`Folder` or *None* if not found """

        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)
        PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

        try:
            # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
            arch_storeid = HrGetOneProp(self.mapiobj, PROP_STORE_ENTRYIDS).Value[0]
            arch_folderid = HrGetOneProp(self.mapiobj, PROP_ITEM_ENTRYIDS).Value[0]
        except MAPIErrorNotFound:
            return

        archive_store = self.server._store2(arch_storeid)
        return Store(mapiobj=archive_store, server=self.server).folder(entryid=codecs.encode(arch_folderid, 'hex'))

    @property
    def primary_store(self):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_REF_STORE_ENTRYID = CHANGE_PROP_TYPE(ids[3], PT_BINARY)
        try:
            entryid = HrGetOneProp(self.mapiobj, PROP_REF_STORE_ENTRYID).Value
        except MAPIErrorNotFound:
            return
        return Store(entryid=entryid, server=self.server)

    @property
    def primary_folder(self):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_REF_ITEM_ENTRYID = CHANGE_PROP_TYPE(ids[4], PT_BINARY)
        entryid = HrGetOneProp(self.mapiobj, PROP_REF_ITEM_ENTRYID).Value

        if self.primary_store:
            try:
                return self.primary_store.folder(entryid=codecs.encode(entryid, 'hex'))
            except MAPIErrorNotFound:
                pass

    @property
    def created(self):
        try:
            return self.prop(PR_CREATION_TIME).value
        except MAPIErrorNotFound:
            pass

    def __eq__(self, f): # XXX check same store?
        if isinstance(f, Folder):
            return self._entryid == f._entryid
        return False

    def __ne__(self, f):
        return not self == f

    def __iter__(self):
        return self.items()

    def __unicode__(self): # XXX associated?
        return u'Folder(%s)' % self.name

    def __repr__(self):
        return _repr(self)

class Item(object):
    """Item class"""

    def __init__(self, parent=None, eml=None, ics=None, vcf=None, load=None, loads=None, attachments=True, create=False, mapiobj=None):
        # TODO: self.folder fix this!
        self.emlfile = eml
        self._folder = None
        if isinstance(parent, Folder):
            self._folder = parent
        # XXX
        self._architem = None

        if mapiobj:
            self.mapiobj = mapiobj
            if isinstance(parent, Store):
                self.server = parent.server
            # XXX

        elif create:
            self.mapiobj = self.folder.mapiobj.CreateMessage(None, 0)
            self.store = self.folder.store
            self.server = server = self.store.server # XXX

            if eml is not None:
                # options for CreateMessage: 0 / MAPI_ASSOCIATED
                dopt = inetmapi.delivery_options()
                inetmapi.IMToMAPI(server.mapisession, self.folder.store.mapiobj, None, self.mapiobj, self.emlfile, dopt)

            elif ics is not None:
                icm = icalmapi.CreateICalToMapi(self.mapiobj, server.ab, False)
                icm.ParseICal(ics, 'utf-8', '', None, 0)
                icm.GetItem(0, 0, self.mapiobj)

            elif vcf is not None:
                import vobject
                v = vobject.readOne(vcf)
                fullname, email = v.fn.value, str(v.email.value)
                self.mapiobj.SetProps([ # XXX fix/remove non-essential props, figure out hardcoded numbers
                    SPropValue(PR_ADDRTYPE, 'SMTP'), SPropValue(PR_BODY, ''),
                    SPropValue(PR_LOCALITY, ''), SPropValue(PR_STATE_OR_PROVINCE, ''),
                    SPropValue(PR_BUSINESS_FAX_NUMBER, ''), SPropValue(PR_COMPANY_NAME, ''),
                    SPropValue(0x8130001F, fullname), SPropValue(0x8132001E, 'SMTP'),
                    SPropValue(0x8133001E, email), SPropValue(0x8134001E, ''),
                    SPropValue(0x81350102, server.ab.CreateOneOff('', 'SMTP', email, 0)), # XXX
                    SPropValue(PR_GIVEN_NAME, ''), SPropValue(PR_MIDDLE_NAME, ''),
                    SPropValue(PR_NORMALIZED_SUBJECT, ''), SPropValue(PR_TITLE, ''),
                    SPropValue(PR_TRANSMITABLE_DISPLAY_NAME, ''),
                    SPropValue(PR_DISPLAY_NAME_W, fullname),
                    SPropValue(0x80D81003, [0]), SPropValue(0x80D90003, 1), 
                    SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Contact'),
                ])

            elif load is not None:
                self.load(load, attachments=attachments)
            elif loads is not None:
                self.loads(loads, attachments=attachments)

            else:
                try:
                    container_class = HrGetOneProp(self.folder.mapiobj, PR_CONTAINER_CLASS).Value
                except MAPIErrorNotFound:
                    self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Note')])
                else:
                    if container_class == 'IPF.Contact': # XXX just skip first 4 chars? 
                        self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Contact')]) # XXX set default props
                    elif container_class == 'IPF.Appointment':
                        self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, u'IPM.Appointment')]) # XXX set default props

            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def _arch_item(self): # open archive store explicitly so we can handle otherwise silenced errors (MAPI errors in mail bodies for example)
        if self._architem is None:
            if self.stubbed:
                ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0)
                PROP_STORE_ENTRYIDS = CHANGE_PROP_TYPE(ids[0], PT_MV_BINARY)
                PROP_ITEM_ENTRYIDS = CHANGE_PROP_TYPE(ids[1], PT_MV_BINARY)

                # support for multiple archives was a mistake, and is not and _should not_ be used. so we just pick nr 0.
                arch_storeid = HrGetOneProp(self.mapiobj, PROP_STORE_ENTRYIDS).Value[0]
                item_entryid = HrGetOneProp(self.mapiobj, PROP_ITEM_ENTRYIDS).Value[0]

                self._architem = self.server._store2(arch_storeid).OpenEntry(item_entryid, None, 0)
            else:
                self._architem = self.mapiobj
        return self._architem

    @property
    def entryid(self):
        """ Item entryid """

        return bin2hex(HrGetOneProp(self.mapiobj, PR_ENTRYID).Value)

    @property
    def hierarchyid(self):
        return HrGetOneProp(self.mapiobj, PR_EC_HIERARCHYID).Value

    @property
    def sourcekey(self):
        """ Item sourcekey """

        if not hasattr(self, '_sourcekey'): # XXX more general caching solution
            self._sourcekey = bin2hex(HrGetOneProp(self.mapiobj, PR_SOURCE_KEY).Value)
        return self._sourcekey

    @property
    def subject(self):
        """ Item subject or *None* if no subject """

        try:
            return self.prop(PR_SUBJECT_W).value
        except MAPIErrorNotFound:
            return u''

    @subject.setter
    def subject(self, x):
        self.mapiobj.SetProps([SPropValue(PR_SUBJECT_W, unicode(x))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def body(self):
        """ Item :class:`body <Body>` """

        return Body(self) # XXX return None if no body..?

    @property
    def size(self):
        """ Item size """

        return self.prop(PR_MESSAGE_SIZE).value

    @property
    def message_class(self):
        return self.prop(PR_MESSAGE_CLASS_W).value

    @message_class.setter
    def message_class(self, messageclass):
        # FIXME: Add all possible PR_MESSAGE_CLASS values
        """
        MAPI Message classes:
        * IPM.Note.SMIME.MultipartSigned - smime signed email
        * IMP.Note                       - normal email
        * IPM.Note.SMIME                 - smime encypted email
        * IPM.StickyNote                 - note
        * IPM.Appointment                - appointment
        * IPM.Task                       - task
        """
        self.mapiobj.SetProps([SPropValue(PR_MESSAGE_CLASS_W, unicode(messageclass))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @body.setter
    def body(self, x):
        self.mapiobj.SetProps([SPropValue(PR_BODY_W, unicode(x))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def created(self):
        try:
            return self.prop(PR_CREATION_TIME).value
        except MAPIErrorNotFound:
            pass

    @property
    def received(self):
        """ Datetime instance with item delivery time """

        try:
            return self.prop(PR_MESSAGE_DELIVERY_TIME).value
        except MAPIErrorNotFound:
            pass

    @property
    def last_modified(self):
        try:
            return self.prop(PR_LAST_MODIFICATION_TIME).value
        except MAPIErrorNotFound:
            pass

    @property
    def stubbed(self):
        """ Is item stubbed by archiver? """

        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX cache folder.GetIDs..?
        PROP_STUBBED = CHANGE_PROP_TYPE(ids[2], PT_BOOLEAN)
        try:
            return HrGetOneProp(self.mapiobj, PROP_STUBBED).Value # False means destubbed
        except MAPIErrorNotFound:
            return False

    @property
    def read(self):
        """ Return boolean which shows if a message has been read """

        return self.prop(PR_MESSAGE_FLAGS).value & MSGFLAG_READ > 0

    @read.setter
    def read(self, value):
        if value:
            self.mapiobj.SetReadFlag(0)
        else:
            self.mapiobj.SetReadFlag(CLEAR_READ_FLAG)

    @property
    def categories(self):
        proptag = self.mapiobj.GetIDsFromNames([NAMED_PROP_CATEGORY], MAPI_CREATE)[0]
        proptag = CHANGE_PROP_TYPE(proptag, PT_MV_STRING8)
        try:
            value = self.prop(proptag).value
        except MAPIErrorNotFound:
            value = []
        return PersistentList(self.mapiobj, proptag, value)

    @categories.setter
    def categories(self, value):
        proptag = self.mapiobj.GetIDsFromNames([NAMED_PROP_CATEGORY], MAPI_CREATE)[0]
        proptag = CHANGE_PROP_TYPE(proptag, PT_MV_STRING8)
        self.mapiobj.SetProps([SPropValue(proptag, list(value))])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def folder(self):
        """ Parent :class:`Folder` of an item """

        if self._folder:
            return self._folder
        try:
            return Folder(self.store, HrGetOneProp(self.mapiobj, PR_PARENT_ENTRYID).Value)
        except MAPIErrorNotFound:
            pass

    @property
    def importance(self):
        """ Importance """

        # TODO: userfriendly repr of value
        try:
            return self.prop(PR_IMPORTANCE).value
        except MAPIErrorNotFound:
            pass

    @importance.setter
    def importance(self, value):
        """ Set importance

        PR_IMPORTANCE_LOW
        PR_IMPORTANCE_MEDIUM
        PR_IMPORTANCE_HIGH
        """

        self.mapiobj.SetProps([SPropValue(PR_IMPORTANCE, value)])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def create_prop(self, proptag, value, proptype=None):
        return _create_prop(self, self.mapiobj, proptag, value, proptype)

    def prop(self, proptag):
        return _prop(self, self.mapiobj, proptag)

    def get_prop(self, proptag):
        try:
            return self.prop(proptag)
        except MAPIErrorNotFound:
            pass

    def props(self, namespace=None):
        return _props(self.mapiobj, namespace)

    def attachments(self, embedded=False):
        """ Return item :class:`attachments <Attachment>`

        :param embedded: include embedded attachments
        """

        mapiitem = self._arch_item
        table = mapiitem.GetAttachmentTable(MAPI_DEFERRED_ERRORS)
        table.SetColumns([PR_ATTACH_NUM, PR_ATTACH_METHOD], TBL_BATCH)
        while True:
            rows = table.QueryRows(50, 0)
            if len(rows) == 0:
                break
            for row in rows:
                if row[1].Value == ATTACH_BY_VALUE or (embedded and row[1].Value == ATTACH_EMBEDDED_MSG):
                    att = mapiitem.OpenAttach(row[0].Value, IID_IAttachment, 0)
                    yield Attachment(att)

    def create_attachment(self, name, data):
        """Create a new attachment

        :param name: the attachment name
        :param data: string containing the attachment data
        """

        # XXX: use file object instead of data?
        (id_, attach) = self.mapiobj.CreateAttach(None, 0)
        name = unicode(name)
        props = [SPropValue(PR_ATTACH_LONG_FILENAME_W, name), SPropValue(PR_ATTACH_METHOD, ATTACH_BY_VALUE)]
        attach.SetProps(props)
        stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, STGM_WRITE|STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE)
        stream.Write(data)
        stream.Commit(0)
        attach.SaveChanges(KEEP_OPEN_READWRITE)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE) # XXX needed?
        return Attachment(attach)

    def header(self, name):
        """ Return transport message header with given name """

        return self.headers().get(name)

    def headers(self):
        """ Return transport message headers """

        try:
            message_headers = self.prop(PR_TRANSPORT_MESSAGE_HEADERS_W)
            headers = email.parser.Parser().parsestr(message_headers.value, headersonly=True)
            return headers
        except MAPIErrorNotFound:
            return {}

    def eml(self):
        """ Return .eml version of item """

        if self.emlfile is None:
            try:
                self.emlfile = _stream(self.mapiobj, PR_EC_IMAP_EMAIL)
            except MAPIErrorNotFound:
                sopt = inetmapi.sending_options()
                sopt.no_recipients_workaround = True
                self.emlfile = inetmapi.IMToINet(self.store.server.mapisession, None, self.mapiobj, sopt)
        return self.emlfile

    def vcf(self): # XXX don't we have this builtin somewhere? very basic for now
        import vobject
        v = vobject.vCard()
        v.add('n')
        v.n.value = vobject.vcard.Name(family='', given='') # XXX
        v.add('fn')
        v.fn.value = ''
        v.add('email')
        v.email.value = ''
        v.email.type_param = 'INTERNET'
        try:
            v.fn.value = HrGetOneProp(self.mapiobj, 0x8130001E).Value
        except MAPIErrorNotFound:
            pass
        try:
            v.email.value = HrGetOneProp(self.mapiobj, 0x8133001E).Value
        except MAPIErrorNotFound:
            pass
        return v.serialize()

    # XXX def ics for ical export?

    def send(self):
        props = []
        props.append(SPropValue(PR_SENTMAIL_ENTRYID, _unhex(self.folder.store.sentmail.entryid)))
        props.append(SPropValue(PR_DELETE_AFTER_SUBMIT, True))
        self.mapiobj.SetProps(props)
        self.mapiobj.SubmitMessage(0)

    @property
    def sender(self):
        """ Sender :class:`Address` """

        addrprops = (PR_SENDER_ADDRTYPE_W, PR_SENDER_NAME_W, PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID)
        args = [self.get_prop(p).value if self.get_prop(p) else None for p in addrprops]
        return Address(self.server, *args)

    @property
    def from_(self):
        """ From :class:`Address` """

        addrprops= (PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, PR_SENT_REPRESENTING_ENTRYID)
        args = [self.get_prop(p).value if self.get_prop(p) else None for p in addrprops]
        return Address(self.server, *args)

    @from_.setter
    def from_(self, addr):
        pr_addrtype, pr_dispname, pr_email, pr_entryid = self._addr_props(addr)
        self.mapiobj.SetProps([
            SPropValue(PR_SENT_REPRESENTING_ADDRTYPE_W, unicode(pr_addrtype)), # XXX pr_addrtype should be unicode already
            SPropValue(PR_SENT_REPRESENTING_NAME_W, pr_dispname),
            SPropValue(PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, pr_email),
            SPropValue(PR_SENT_REPRESENTING_ENTRYID, pr_entryid),
        ])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def table(self, name, restriction=None, order=None, columns=None):
        return Table(self.server, self.mapiobj.OpenProperty(name, IID_IMAPITable, MAPI_UNICODE, 0), name, restriction=restriction, order=order, columns=columns)

    def tables(self):
        yield self.table(PR_MESSAGE_RECIPIENTS)
        yield self.table(PR_MESSAGE_ATTACHMENTS)

    def recipients(self, _type=None):
        """ Return recipient :class:`addresses <Address>` """

        for row in self.table(PR_MESSAGE_RECIPIENTS):
            row = dict([(x.proptag, x) for x in row])
            if not _type or row[PR_RECIPIENT_TYPE].value == _type:
                args = [row[p].value if p in row else None for p in (PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_EMAIL_ADDRESS_W, PR_ENTRYID)]
                yield Address(self.server, *args)

    @property
    def to(self):
        return self.recipients(_type=MAPI_TO)

    @property
    def cc(self):
        return self.recipients(_type=MAPI_CC)

    @property
    def bcc(self):
        return self.recipients(_type=MAPI_BCC)

    @property
    def start(self): # XXX optimize, guid
        return self.prop('common:34070').value

    @property
    def end(self): # XXX optimize, guid
        return self.prop('common:34071').value

    @property
    def recurring(self):
        return self.prop('appointment:33315').value

    @property
    def recurrence(self):
        return Recurrence(self)

    def occurrences(self, start=None, end=None):
        try:
            if self.recurring:
                recurrences = self.recurrence.recurrences
                if start and end:
                    recurrences = recurrences.between(start, end)
                for d in recurrences:
                    occ = Occurrence(self, d, d+datetime.timedelta(hours=1)) # XXX
                    if (not start or occ.start >= start) and (not end or occ.end < end): # XXX slow for now; overlaps with start, end?
                        yield occ
            else:
                occ = Occurrence(self, self.start, self.end)
                if (not start or occ.start >= start) and (not end or occ.end < end):
                    yield occ
        except MAPIErrorNotFound: # XXX shouldn't happen
            pass

    def _addr_props(self, addr):
        if isinstance(addr, User):
            pr_addrtype = 'ZARAFA'
            pr_dispname = addr.name
            pr_email = addr.email
            pr_entryid = _unhex(addr.userid)
        else:
            addr = unicode(addr)
            pr_addrtype = 'SMTP'
            pr_dispname, pr_email = email.utils.parseaddr(addr)
            pr_dispname = pr_dispname or u'nobody'
            pr_entryid = self.server.ab.CreateOneOff(pr_dispname, u'SMTP', unicode(pr_email), MAPI_UNICODE)
        return pr_addrtype, pr_dispname, pr_email, pr_entryid

    @to.setter
    def to(self, addrs):
        if _is_str(addrs):
            addrs = unicode(addrs).split(';')
        elif isinstance(addrs, User):
            addrs = [addrs]
        names = []
        for addr in addrs:
            pr_addrtype, pr_dispname, pr_email, pr_entryid = self._addr_props(addr)
            names.append([
                SPropValue(PR_RECIPIENT_TYPE, MAPI_TO), 
                SPropValue(PR_DISPLAY_NAME_W, pr_dispname),
                SPropValue(PR_ADDRTYPE_W, unicode(pr_addrtype)),
                SPropValue(PR_EMAIL_ADDRESS_W, unicode(pr_email)),
                SPropValue(PR_ENTRYID, pr_entryid),
            ])
        self.mapiobj.ModifyRecipients(0, names)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE) # XXX needed?

    def delete(self, items):
        """Delete properties or attachments from an Item

        :param items: The Attachments or Properties
        """

        if isinstance(items, (Attachment, Property)):
            items = [items]
        else:
            items = list(items)

        attach_ids = [item.number for item in items if isinstance(item, Attachment)]
        proptags = [item.proptag for item in items if isinstance(item, Property)]
        if proptags:
            self.mapiobj.DeleteProps(proptags)
        for attach_id in attach_ids:
            self._arch_item.DeleteAttach(attach_id, 0, None, 0)

        # XXX: refresh the mapiobj since PR_ATTACH_NUM is updated when opening
        # an message? PR_HASATTACH is also updated by the server.
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def _convert_to_smtp(self, props, tag_data):
        if not hasattr(self.server, '_smtp_cache'): # XXX speed hack, discuss
            self.server._smtp_cache = {}
        for addrtype, email, entryid, name, searchkey in ADDR_PROPS:
            if addrtype not in tag_data or entryid not in tag_data or name not in tag_data: 
                continue
            if tag_data[addrtype][1] in (u'SMTP', u'MAPIPDL'): # XXX MAPIPDL==distlist.. can we just dump this?
                continue
            eid = tag_data[entryid][1]
            if eid in self.server._smtp_cache:
                email_addr = self.server._smtp_cache[eid]
            else:
                try:
                    mailuser = self.server.ab.OpenEntry(eid, IID_IMailUser, 0)
                    email_addr = HrGetOneProp(mailuser, PR_SMTP_ADDRESS_W).Value
                except MAPIErrorUnknownEntryid: # XXX corrupt data? keep going but log problem
                    continue
                except MAPIErrorNotFound: # XXX deleted user, or no email address? or user with multiple entryids..heh?
                    continue
                except MAPIErrorInterfaceNotSupported: # XXX ZARAFA group?
                    continue
                self.server._smtp_cache[eid] = email_addr
            tag_data[addrtype][1] = u'SMTP'
            if email in tag_data:
                tag_data[email][1] = email_addr
            else:
                props.append([email, email_addr, None])
            tag_data[entryid][1] = self.server.ab.CreateOneOff(tag_data[name][1], u'SMTP', email_addr, MAPI_UNICODE)
            key = b'SMTP:'+email_addr.upper().encode('ascii')
            if searchkey in tag_data: # XXX probably need to create, also email
                tag_data[searchkey][1] = key
            else:
                props.append([searchkey, key, None])

    def _dump(self, attachments=True, archiver=True, skip_broken=False):
        # props
        props = []
        tag_data = {}
        bestbody = _bestbody(self.mapiobj)
        for prop in self.props():
            if (bestbody != PR_NULL and prop.proptag in (PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED) and prop.proptag != bestbody):
                continue
            if prop.named: # named prop: prop.id_ system dependant..
                data = [prop.proptag, prop.mapiobj.Value, self.mapiobj.GetNamesFromIDs([prop.proptag], None, 0)[0]]
                if not archiver and data[2].guid == PSETID_Archive:
                    continue
            else:
                data = [prop.proptag, prop.mapiobj.Value, None]
            props.append(data)
            tag_data[prop.proptag] = data
        self._convert_to_smtp(props, tag_data)

        # recipients
        recs = []
        for row in self.table(PR_MESSAGE_RECIPIENTS):
            rprops = []
            tag_data = {}
            for prop in row:
                data = [prop.proptag, prop.mapiobj.Value, None]
                rprops.append(data)
                tag_data[prop.proptag] = data
            recs.append(rprops)
            self._convert_to_smtp(rprops, tag_data)

        # attachments
        atts = []
        # XXX optimize by looking at PR_MESSAGE_FLAGS?
        for row in self.table(PR_MESSAGE_ATTACHMENTS).dict_rows(): # XXX should we use GetAttachmentTable?
            try:
                num = row[PR_ATTACH_NUM]
                method = row[PR_ATTACH_METHOD] # XXX default
                att = self.mapiobj.OpenAttach(num, IID_IAttachment, 0)
                if method == ATTACH_EMBEDDED_MSG:
                    msg = att.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_MODIFY | MAPI_DEFERRED_ERRORS)
                    item = Item(mapiobj=msg)
                    item.server = self.server # XXX
                    data = item._dump() # recursion
                    atts.append(([[a, b, None] for a, b in row.items()], data))
                elif method == ATTACH_BY_VALUE and attachments:
                    data = _stream(att, PR_ATTACH_DATA_BIN)
                    atts.append(([[a, b, None] for a, b in row.items()], data))
            except Exception as e: # XXX generalize so usable in more places
                service = self.server.service
                log = (service or self.server).log
                if log:
                    log.error('could not serialize attachment')
                if skip_broken:
                    if log:
                        log.error(traceback.format_exc(e))
                    if service and service.stats:
                        service.stats['errors'] += 1
                else:
                    raise

        return {
            b'props': props,
            b'recipients': recs,
            b'attachments': atts,
        }

    def dump(self, f, attachments=True, archiver=True):
        pickle.dump(self._dump(attachments=attachments, archiver=archiver), f, pickle.HIGHEST_PROTOCOL)

    def dumps(self, attachments=True, archiver=True, skip_broken=False):
        return pickle.dumps(self._dump(attachments=attachments, archiver=archiver, skip_broken=skip_broken), pickle.HIGHEST_PROTOCOL)

    def _load(self, d, attachments):
        # props
        props = []
        for proptag, value, nameid in d[b'props']:
            if nameid is not None:
                proptag = self.mapiobj.GetIDsFromNames([nameid], MAPI_CREATE)[0] | (proptag & 0xffff)
            props.append(SPropValue(proptag, value))
        self.mapiobj.SetProps(props)

        # recipients
        recipients = [[SPropValue(proptag, value) for (proptag, value, nameid) in row] for row in d[b'recipients']]
        self.mapiobj.ModifyRecipients(0, recipients)

        # attachments
        for props, data in d[b'attachments']:
            props = [SPropValue(proptag, value) for (proptag, value, nameid) in props]
            (id_, attach) = self.mapiobj.CreateAttach(None, 0)
            attach.SetProps(props)
            if isinstance(data, dict): # embedded message
                msg = attach.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY)
                item = Item(mapiobj=msg)
                item._load(data, attachments) # recursion
            elif attachments:
                stream = attach.OpenProperty(PR_ATTACH_DATA_BIN, IID_IStream, STGM_WRITE|STGM_TRANSACTED, MAPI_MODIFY | MAPI_CREATE)
                stream.Write(data)
                stream.Commit(0)
            attach.SaveChanges(KEEP_OPEN_READWRITE)
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE) # XXX needed?

    def load(self, f, attachments=True):
        self._load(_pickle_load(f), attachments)

    def loads(self, s, attachments=True):
        self._load(_pickle_loads(s), attachments)

    @property
    def embedded(self): # XXX multiple?
        for row in self.table(PR_MESSAGE_ATTACHMENTS).dict_rows(): # XXX should we use GetAttachmentTable?
            num = row[PR_ATTACH_NUM]
            method = row[PR_ATTACH_METHOD] # XXX default
            if method == ATTACH_EMBEDDED_MSG:
                att = self.mapiobj.OpenAttach(num, IID_IAttachment, 0)
                msg = att.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_MODIFY | MAPI_DEFERRED_ERRORS)
                item = Item(mapiobj=msg)
                item.server = self.server # XXX
                return item

    @property
    def primary_item(self):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX merge namedprops stuff
        PROP_REF_ITEM_ENTRYID = CHANGE_PROP_TYPE(ids[4], PT_BINARY)
        entryid = HrGetOneProp(self.mapiobj, PROP_REF_ITEM_ENTRYID).Value

        try:
            return self.folder.primary_store.item(entryid=codecs.encode(entryid, 'hex'))
        except MAPIErrorNotFound:
            pass

    def restore(self):
        ids = self.mapiobj.GetIDsFromNames(NAMED_PROPS_ARCHIVER, 0) # XXX cache folder.GetIDs..?
        PROP_STUBBED = CHANGE_PROP_TYPE(ids[2], PT_BOOLEAN)
        PROP_REF_STORE_ENTRYID = CHANGE_PROP_TYPE(ids[3], PT_BINARY)
        PROP_REF_ITEM_ENTRYID = CHANGE_PROP_TYPE(ids[4], PT_BINARY)
        PROP_REF_PREV_ENTRYID = CHANGE_PROP_TYPE(ids[5], PT_BINARY)
        PROP_FLAGS = CHANGE_PROP_TYPE(ids[6], PT_LONG)

        # get/create primary item
        primary_item = self.primary_item
        if not primary_item:
            mapiobj = self.folder.primary_folder.mapiobj.CreateMessage(None, 0)
            new = True
        else:
            mapiobj = primary_item.mapiobj
            new = False

        if not new and not primary_item.stubbed:
            return

        # cleanup primary item
        mapiobj.DeleteProps([PROP_STUBBED, PR_ICON_INDEX])
        at = mapiobj.GetAttachmentTable(0)
        at.SetColumns([PR_ATTACH_NUM], TBL_BATCH)
        while True:
            rows = at.QueryRows(20, 0)
            if len(rows) == 0:
                break
            for row in rows:
                mapiobj.DeleteAttach(row[0].Value, 0, None, 0)

        # copy contents into it
        exclude_props = [PROP_REF_STORE_ENTRYID, PROP_REF_ITEM_ENTRYID, PROP_REF_PREV_ENTRYID, PROP_FLAGS]
        self.mapiobj.CopyTo(None, exclude_props, 0, None, IID_IMessage, mapiobj, 0)

        mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

        # update backref
        if new:
            entryid = HrGetOneProp(mapiobj, PR_ENTRYID).Value
            self.mapiobj.SetProps([SPropValue(PROP_REF_ITEM_ENTRYID, entryid)])
            self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def __unicode__(self):
        return u'Item(%s)' % self.subject

    def __repr__(self):
        return _repr(self)

class Body:
    """Item Body class"""

    # XXX XXX setters!

    def __init__(self, mapiitem):
        self.mapiitem = mapiitem

    @property
    def text(self):
        """ Plain text representation """

        try:
            mapiitem = self.mapiitem._arch_item # XXX server already goes 'underwater'.. check details
            return _stream(mapiitem, PR_BODY_W) # under windows them be utf-16le?
        except MAPIErrorNotFound:
            return u''

    @property
    def html(self):
        """ HTML representation """

        try:
            mapiitem = self.mapiitem._arch_item
            return _stream(mapiitem, PR_HTML)
        except MAPIErrorNotFound:
            return ''

    @property
    def rtf(self):
        """ RTF representation """

        try:
            mapiitem = self.mapiitem._arch_item
            return _stream(mapiitem, PR_RTF_COMPRESSED)
        except MAPIErrorNotFound:
            return ''

    @property
    def type_(self):
        """ original body type: 'text', 'html', 'rtf' or None if it cannot be determined """
        tag = _bestbody(self.mapiitem.mapiobj)
        if tag == PR_BODY_W:
            return 'text'
        elif tag == PR_HTML:
            return 'html'
        elif tag == PR_RTF_COMPRESSED:
            return 'rtf'

    def __unicode__(self):
        return u'Body()'

    def __repr__(self):
        return _repr(self)

class Occurrence(object):
    def __init__(self, item, start, end):
        self.item = item
        self.start = start
        self.end = end

    def __getattr__(self, x):
        return getattr(self.item, x)

    def __unicode__(self):
        return u'Occurrence(%s)' % self.subject

    def __repr__(self):
        return _repr(self)


class Recurrence:
    def __init__(self, item): # XXX just readable start/end for now
        from dateutil.rrule import WEEKLY, DAILY, MONTHLY, MO, TU, TH, FR, WE, SA, SU, rrule, rruleset
        from datetime import timedelta
        # TODO: add check if we actually have a recurrence, otherwise we throw a mapi exception which might not be desirable
        self.item = item
        value = item.prop('appointment:33302').value # recurrencestate
        SHORT, LONG = 2, 4
        pos = 5 * SHORT + 3 * LONG 

        self.recurrence_frequency = _unpack_short(value, 2 * SHORT)
        self.patterntype = _unpack_short(value, 3 * SHORT)
        self.calendar_type = _unpack_short(value, 4 * SHORT)
        self.first_datetime = _unpack_long(value, 5 * SHORT)
        self.period = _unpack_long(value , 5 * SHORT + LONG) # 12 for year, coincedence?

        if self.patterntype == 1: # Weekly recurrence
            self.pattern = _unpack_long(value, pos) # WeekDays
            pos += LONG
        if self.patterntype in (2, 4, 10, 12): # Monthly recurrence
            self.pattern = _unpack_long(value, pos) # Day Of Month
            pos += LONG
        elif self.patterntype in (3, 11): # Yearly recurrence
            weekday = _unpack_long(value, pos)
            pos += LONG 
            weeknumber = _unpack_long(value, pos)
            pos += LONG 

        self.endtype = _unpack_long(value, pos)
        pos += LONG
        self.occurrence_count = _unpack_long(value, pos)
        pos += LONG
        self.first_dow = _unpack_long(value, pos)
        pos += LONG

        # Number of ocurrences which have been removed in a recurrene
        self.delcount = _unpack_long(value, pos)
        pos += LONG
        # XXX: optimize?
        self.del_recurrences = []
        for _ in range(0, self.delcount):
            self.del_recurrences.append(datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos))))
            pos += LONG

        self.modcount = _unpack_long(value, pos)
        pos += LONG
        # XXX: optimize?
        self.mod_recurrences = []
        for _ in range(0, self.modcount):
            self.mod_recurrences.append(datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos))))
            pos += LONG

        self.start = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))
        pos += LONG
        self.end = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))

        pos += 3 * LONG # ulReaderVersion2, ulReaderWriter2
        self.startime_offset = _unpack_long(value, pos) # XXX: type?
        pos += LONG
        self.endtime_offset = _unpack_long(value, pos) # XXX: type?
        pos += LONG

        self.start = datetime.datetime(self.start.year, self.start.month, self.start.day) + datetime.timedelta(minutes=self.startime_offset)
        self.end = datetime.datetime(self.end.year, self.end.month, self.end.day) + datetime.timedelta(minutes=self.endtime_offset)

        # Exceptions
        self.exception_count = _unpack_short(value, pos)
        pos += SHORT

        # FIXME: create class instances.
        self.exceptions = []
        for i in range(0, self.exception_count):
            exception = {}
            # Blegh helper..
            exception['startdatetime'] = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))
            pos += LONG
            exception['enddatetime'] = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))
            pos += LONG
            exception['originalstartdate'] = datetime.datetime.fromtimestamp(_rectime_to_unixtime(_unpack_long(value, pos)))
            pos += LONG
            exception['overrideflags'] = _unpack_short(value, pos)
            pos += SHORT

            # We have modified the subject
            if exception['overrideflags'] & ARO_SUBJECT:
                subject_length1 = _unpack_short(value, pos) # XXX: unused?
                pos += SHORT
                subject_length2 = _unpack_short(value, pos)
                pos += SHORT
                exception['subject'] = _unpack_string(value, pos, subject_length2)
                pos += subject_length2

            # XXX: Changed the meeting type too???
            if exception['overrideflags'] & ARO_MEETINGTYPE:
                exception['meetingtype'] = _unpack_long(value, pos)
                pos += LONG

            if exception['overrideflags'] & ARO_REMINDERDELTA:
                exception['reminderdelta'] = _unpack_long(value, pos) # XXX: datetime?
                pos += LONG

            if exception['overrideflags'] & ARO_REMINDERSET:
                exception['reminderset'] = _unpack_long(value, pos) # XXX: bool?
                pos += LONG

            if exception['overrideflags'] & ARO_LOCATION:
                localation_length1 = _unpack_short(value, pos) # XXX: unused?
                pos += SHORT
                location_length2 = _unpack_short(value, pos)
                pos += SHORT
                exception['location'] = _unpack_string(value, pos, location_length2)
                pos += location_length2

            if exception['overrideflags'] & ARO_BUSYSTATUS:
                exception['busystatus'] = _unpack_long(value, pos)
                pos += LONG

            if exception['overrideflags'] & ARO_ATTACHMENT:
                exception['attachment'] = _unpack_long(value, pos)
                pos += LONG

            if exception['overrideflags'] & ARO_SUBTYPE:
                exception['subtype'] = _unpack_long(value, pos)
                pos += LONG

            if exception['overrideflags'] & ARO_APPTCOLOR:
                exception['color'] = _unpack_long(value, pos)
                pos += LONG

            self.exceptions.append(exception)


        # FIXME: move to class Item? XXX also some of these properties do not seem to exist when syncing over Z-push
#        self.clipend = item.prop('appointment:33334').value
#        self.clipstart = item.prop('appointment:33333').value
        self.recurrence_pattern = item.prop('appointment:33330').value
#        self.invited = item.prop('appointment:33321').value

        # FIXME; doesn't dateutil have a list of this?
        rrule_weekdays = {0: SU, 1: MO, 2: TU, 3: WE, 4: TH, 5: FR, 6: SA} # FIXME: remove above

        # FIXME: add DAILY, patterntype == 0
        # FIXME: merge exception details with normal appointment data to recurrence.occurences() (Class occurence)
        if self.patterntype == 1: # WEEKLY
            byweekday = () # Set
            for index, week in rrule_weekdays.items():
                if (self.pattern >> index ) & 1:
                    byweekday += (week,)
            # Setup our rule
            rule = rruleset()
            # FIXME: add one day, so that we don't miss the last recurrence, since the end date is for example 11-3-2015 on 1:00
            # But the recurrence is on 8:00 that day and we should include it.
            rule.rrule(rrule(WEEKLY, dtstart=self.start, until=self.end + timedelta(days=1), byweekday=byweekday))

            self.recurrences = rule
            #self.recurrences = rrule(WEEKLY, dtstart=self.start, until=self.end, byweekday=byweekday)
        elif self.patterntype == 2: # MONTHLY
            # X Day of every Y month(s)
            # The Xnd Y (day) of every Z Month(s)
            self.recurrences = rrule(MONTHLY, dtstart=self.start, until=self.end, bymonthday=self.pattern, interval=self.period)
            # self.pattern is either day of month or 
        elif self.patterntype == 3: # MONTHY, YEARLY
            byweekday = () # Set
            for index, week in rrule_weekdays.items():
                if (weekday >> index ) & 1:
                    byweekday += (week(weeknumber),)
            # Yearly, the last XX of YY
            self.recurrences = rrule(MONTHLY, dtstart=self.start, until=self.end, interval=self.period, byweekday=byweekday)

        # Remove deleted ocurrences
        for del_date in self.del_recurrences:
            # XXX: Somehow rule.rdate does not work in combination with rule.exdate
            del_date = datetime.datetime(del_date.year, del_date.month, del_date.day, self.start.hour, self.start.minute)
            if not del_date in self.mod_recurrences:
                rule.exdate(del_date)

        # add exceptions
        for exception in self.exceptions:
            rule.rdate(exception['startdatetime'])


    def __unicode__(self):
        return u'Recurrence(start=%s - end=%s)' % (self.start, self.end)

    def __repr__(self):
        return _repr(self)


class Outofoffice(object):
    """Outofoffice class

    Class which contains a :class:`store <Store>` out of office properties and
    can set out-of-office status, message and subject.

    :param store: :class:`store <Store>`
    """
    def __init__(self, store):
        self.store = store

    @property
    def enabled(self):
        """ Out of office enabled status """

        try:
            return self.store.prop(PR_EC_OUTOFOFFICE).value
        except MAPIErrorNotFound:
            return False

    @enabled.setter
    def enabled(self, value):
        self.store.mapiobj.SetProps([SPropValue(PR_EC_OUTOFOFFICE, value)])
        self.store.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def subject(self):
        """ Subject """

        try:
            return self.store.prop(PR_EC_OUTOFOFFICE_SUBJECT_W).value
        except MAPIErrorNotFound:
            return u''

    @subject.setter
    def subject(self, value):
        self.store.mapiobj.SetProps([SPropValue(PR_EC_OUTOFOFFICE_SUBJECT_W, unicode(value))])
        self.store.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def message(self):
        """ Message """

        try:
            return self.store.prop(PR_EC_OUTOFOFFICE_MSG_W).value
        except MAPIErrorNotFound:
            return u''

    @message.setter
    def message(self, value):
        self.store.mapiobj.SetProps([SPropValue(PR_EC_OUTOFOFFICE_MSG_W, unicode(value))])
        self.store.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def start(self):
        """ Out-of-office is activated from the particular datetime onwards """
        try:
            return self.store.prop(PR_EC_OUTOFOFFICE_FROM).value
        except MAPIErrorNotFound:
            pass

    @start.setter
    def start(self, value):
        if value is None:
            self.store.mapiobj.DeleteProps([PR_EC_OUTOFOFFICE_FROM])
        else:
            value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
            self.store.mapiobj.SetProps([SPropValue(PR_EC_OUTOFOFFICE_FROM, value)])
        self.store.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    @property
    def end(self):
        """ Out-of-office is activated until the particular datetime """
        try:
            return self.store.prop(PR_EC_OUTOFOFFICE_UNTIL).value
        except MAPIErrorNotFound:
            pass

    @end.setter
    def end(self, value):
        if value is None:
            self.store.mapiobj.DeleteProps([PR_EC_OUTOFOFFICE_UNTIL])
        else:
            value = MAPI.Time.unixtime(time.mktime(value.timetuple()))
            self.store.mapiobj.SetProps([SPropValue(PR_EC_OUTOFOFFICE_UNTIL, value)])
        self.store.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def __unicode__(self):
        return u'Outofoffice(%s)' % self.subject

    def __repr__(self):
        return _repr(self)

    def update(self, **kwargs):
        """ Update function for outofoffice """

        for key, val in kwargs.items():
            setattr(self, key, val)

class Address:
    """Address class"""

    def __init__(self, server=None, addrtype=None, name=None, email=None, entryid=None):
        self.server = server
        self.addrtype = addrtype
        self._name = name
        self._email = email
        self.entryid = entryid

    @property
    def name(self):
        """ Full name """

        return self._name or u''

    @property
    def email(self):
        """ Email address """

        if self.addrtype == 'ZARAFA':
            return self.server._resolve_email(entryid=self.entryid)
        else:
            return self._email or ''

    def __unicode__(self):
        return u'Address(%s)' % (self.name or self.email)

    def __repr__(self):
        return _repr(self)

class Attachment(object):
    """Attachment class"""

    def __init__(self, mapiobj):
        self.mapiobj = mapiobj
        self._data = None

    @property
    def hierarchyid(self):
        return self.prop(PR_EC_HIERARCHYID).value

    @property
    def number(self):
        try:
            return HrGetOneProp(self.mapiobj, PR_ATTACH_NUM).Value
        except MAPIErrorNotFound:
            return 0

    @property
    def mimetype(self):
        """ Mime-type or *None* if not found """

        try:
            return HrGetOneProp(self.mapiobj, PR_ATTACH_MIME_TAG_W).Value
        except MAPIErrorNotFound:
            pass

    @property
    def filename(self):
        """ Filename or *None* if not found """

        try:
            return HrGetOneProp(self.mapiobj, PR_ATTACH_LONG_FILENAME_W).Value
        except MAPIErrorNotFound:
            pass

    @property
    def size(self):
        """ Size """
        # XXX size of the attachment object, so more than just the attachment data
        # XXX (useful when calculating store size, for example.. sounds interesting to fix here)
        try:
            return int(HrGetOneProp(self.mapiobj, PR_ATTACH_SIZE).Value)
        except MAPIErrorNotFound:
            return 0 # XXX

    def __len__(self):
        return self.size

    @property
    def data(self):
        """ Binary data """

        if self._data is None:
            self._data = _stream(self.mapiobj, PR_ATTACH_DATA_BIN)
        return self._data

    # file-like behaviour
    def read(self):
        return self.data

    @property
    def name(self):
        return self.filename

    def prop(self, proptag):
        return _prop(self, self.mapiobj, proptag)

    def props(self):
        return _props(self.mapiobj)

    def __unicode__(self):
        return u'Attachment("%s")' % self.name

    def __repr__(self):
        return _repr(self)

class User(object):
    """User class"""

    def __init__(self, name=None, server=None, email=None):
        server = server or Server()
        self.server = server

        if email:
            try:
                self._name = unicode(server.gab.ResolveNames([PR_EMAIL_ADDRESS_W], MAPI_UNICODE | EMS_AB_ADDRESS_LOOKUP, [[SPropValue(PR_DISPLAY_NAME_W, unicode(email))]], [MAPI_UNRESOLVED])[0][0][1].Value)
            except (MAPIErrorNotFound, MAPIErrorInvalidParameter):
                raise NotFoundError("no such user '%s'" % email)
        else:
            self._name = unicode(name)

        try:
            self._ecuser = self.server.sa.GetUser(self.server.sa.ResolveUserName(self._name, MAPI_UNICODE), MAPI_UNICODE)
        except (MAPIErrorNotFound, MAPIErrorInvalidParameter): # multi-tenant, but no '@' in username..
            raise NotFoundError("no such user: '%s'" % self.name)
        self._mapiobj = None

    @property
    def mapiobj(self):
        if not self._mapiobj:
            self._mapiobj = self.server.mapisession.OpenEntry(self._ecuser.UserID, None, 0)
        return self._mapiobj

    @property
    def admin(self):
        return self._ecuser.IsAdmin == 1

    @admin.setter
    def admin(self, value):
        self._update(admin=value)

    @property
    def name(self):
        """ Account name """

        return self._name

    @name.setter
    def name(self, value):
        self._update(username=unicode(value))

    @property
    def fullname(self):
        """ Full name """

        return self._ecuser.FullName

    @fullname.setter
    def fullname(self, value):
        self._update(fullname=unicode(value))

    @property
    def email(self):
        """ Email address """

        return self._ecuser.Email

    @email.setter
    def email(self, value):
        self._update(email=unicode(value))

    @property
    def features(self):
        """ Enabled features (pop3/imap/mobile) """

        if not hasattr(self._ecuser, 'MVPropMap'):
            raise NotSupportedError('Python-Mapi does not support MVPropMap')

        for entry in self._ecuser.MVPropMap:
            if entry.ulPropId == PR_EC_ENABLED_FEATURES_W:
                return entry.Values

    @features.setter
    def features(self, value):

        if not hasattr(self._ecuser, 'MVPropMap'):
            raise NotSupportedError('Python-Mapi does not support MVPropMap')

        # Enabled + Disabled defines all features.
        features = set([e for entry in self._ecuser.MVPropMap for e in entry.Values])
        disabled = list(features - set(value))

        # XXX: performance
        for entry in self._ecuser.MVPropMap:
            if entry.ulPropId == PR_EC_ENABLED_FEATURES_W:
                entry.Values = value
            if entry.ulPropId == PR_EC_DISABLED_FEATURES_W:
                entry.Values = disabled

        self._update()

    def add_feature(self, feature):
        """ Add a feature for a user

        :param feature: the new feature
        """

        self.features = self.features + [unicode(feature)]

    def remove_feature(self, feature):
        """ Remove a feature for a user

        :param feature: the to be removed feature
        """

        # Copy features otherwise we will miss an disabled feature.
        # XXX: improvement?
        features = self.features[:]
        features.remove(unicode(feature))
        self.features = features
        self._update()

    @property
    def userid(self):
        """ Userid """

        return bin2hex(self._ecuser.UserID)

    @property
    def company(self):
        """ :class:`Company` the user belongs to """

        try:
            return Company(HrGetOneProp(self.mapiobj, PR_EC_COMPANY_NAME_W).Value, self.server)
        except MAPIErrorNoSupport:
            return Company(u'Default', self.server)

    @property # XXX
    def local(self):
        store = self.store
        return bool(store and (self.server.guid == bin2hex(HrGetOneProp(store.mapiobj, PR_MAPPING_SIGNATURE).Value)))

    @property
    def store(self):
        """ Default :class:`Store` for user or *None* if no store is attached """

        try:
            entryid = self.server.ems.CreateStoreEntryID(None, self._name, MAPI_UNICODE)
            return Store(entryid=entryid, server=self.server)
        except MAPIErrorNotFound:
            pass

    # XXX deprecated? user.store = .., user.archive_store = ..
    def hook(self, store): # XXX add Company.(un)hook for public store
        self.server.sa.HookStore(ECSTORE_TYPE_PRIVATE, _unhex(self.userid), _unhex(store.guid))

    # XXX deprecated? user.store = None
    def unhook(self):
        return self.server.sa.UnhookStore(ECSTORE_TYPE_PRIVATE, _unhex(self.userid))

    @property
    def active(self):
        return self._ecuser.Class == ACTIVE_USER

    @active.setter
    def active(self, value):
        if value:
            self._update(user_class=ACTIVE_USER)
        else:
            self._update(user_class=NONACTIVE_USER)

    @property
    def home_server(self):
        return self._ecuser.Servername

    @property
    def archive_server(self):
        try:
            return HrGetOneProp(self.mapiobj, PR_EC_ARCHIVE_SERVERS).Value[0]
        except MAPIErrorNotFound:
            return

    def prop(self, proptag):
        return _prop(self, self.mapiobj, proptag)

    def props(self):
        return _props(self.mapiobj)

    @property
    def quota(self):
        """ User :class:`Quota` """

        return Quota(self.server, self._ecuser.UserID)

    @property
    def outofoffice(self):
        """ User :class:`Outofoffice` """

        return self.store.outofoffice

    def groups(self):
        for g in self.server.sa.GetGroupListOfUser(self._ecuser.UserID, MAPI_UNICODE):
            yield Group(g.Groupname, self.server)


    def rules(self):
        return self.inbox.rules()

    def __eq__(self, u): # XXX check same server?
        if isinstance(u, User):
            return self.userid == u.userid
        return False

    def __ne__(self, u):
        return not self == u

    def __unicode__(self):
        return u"User('%s')" % self._name

    def __repr__(self):
        return _repr(self)

    def _update(self, **kwargs):
        username = kwargs.get('username', self.name)
        password = kwargs.get('password', self._ecuser.Password)
        email = kwargs.get('email', unicode(self._ecuser.Email))
        fullname = kwargs.get('fullname', unicode(self._ecuser.FullName))
        user_class = kwargs.get('user_class', self._ecuser.Class)
        admin = kwargs.get('admin', self._ecuser.IsAdmin)

        # Thrown when a user tries to set his own features, handle gracefully otherwise you'll end up without a store
        try:
            # Pass the MVPropMAP otherwise the set values are reset
            if hasattr(self._ecuser, 'MVPropMap'):
                usereid = self.server.sa.SetUser(ECUSER(Username=username, Password=password, Email=email, FullName=fullname,
                                             Class=user_class, UserID=self._ecuser.UserID, IsAdmin=admin, MVPropMap = self._ecuser.MVPropMap), MAPI_UNICODE)

            else:
                usereid = self.server.sa.SetUser(ECUSER(Username=username, Password=password, Email=email, FullName=fullname,
                                             Class=user_class, UserID=self._ecuser.UserID, IsAdmin=admin), MAPI_UNICODE)
        except MAPIErrorNoSupport:
            pass

        self._ecuser = self.server.sa.GetUser(self.server.sa.ResolveUserName(username, MAPI_UNICODE), MAPI_UNICODE)
        if self.name != username:
            self._name = username

        return self

    def __getattr__(self, x): # XXX add __setattr__, e.g. for 'user.archive_store = None'
        store = self.store
        if store:
            return getattr(store, x)

class Quota(object):
    """Quota class

    Quota limits are stored in bytes.
    """

    def __init__(self, server, userid):
        self.server = server
        self.userid = userid
        self._warning_limit = self._soft_limit = self._hard_limit = 0 # XXX quota for 'default' company?
        if userid:
            quota = server.sa.GetQuota(userid, False)
            self._warning_limit = quota.llWarnSize
            self._soft_limit = quota.llSoftSize
            self._hard_limit = quota.llHardSize
            # XXX: logical name for variable
            # Use default quota set in /etc/kopano/server.cfg
            self._use_default_quota = quota.bUseDefaultQuota
            # XXX: is this for multitendancy?
            self._isuser_default_quota = quota.bIsUserDefaultQuota

    @property
    def warning_limit(self):
        """ Warning limit """

        return self._warning_limit

    @warning_limit.setter
    def warning_limit(self, value):
        self.update(warning_limit=value)

    @property
    def soft_limit(self):
        """ Soft limit """

        return self._soft_limit

    @soft_limit.setter
    def soft_limit(self, value):
        self.update(soft_limit=value)

    @property
    def hard_limit(self):
        """ Hard limit """

        return self._hard_limit

    @hard_limit.setter
    def hard_limit(self, value):
        self.update(hard_limit=value)

    def update(self, **kwargs):
        """
        Update function for Quota limits, currently supports the
        following kwargs: `warning_limit`, `soft_limit` and `hard_limit`.

        TODO: support defaultQuota and IsuserDefaultQuota
        """

        self._warning_limit = kwargs.get('warning_limit', self._warning_limit)
        self._soft_limit = kwargs.get('soft_limit', self._soft_limit)
        self._hard_limit = kwargs.get('hard_limit', self._hard_limit)
        # TODO: implement setting defaultQuota, userdefaultQuota
        # (self, bUseDefaultQuota, bIsUserDefaultQuota, llWarnSize, llSoftSize, llHardSize)
        quota = ECQUOTA(False, False, self._warning_limit, self._soft_limit, self._hard_limit)
        self.server.sa.SetQuota(self.userid, quota)

    @property
    def recipients(self):
        if self.userid:
            for ecuser in self.server.sa.GetQuotaRecipients(self.userid, 0):
                yield self.server.user(ecuser.Username)

    def __unicode__(self):
        return u'Quota(warning=%s, soft=%s, hard=%s)' % (_bytes_to_human(self.warning_limit), _bytes_to_human(self.soft_limit), _bytes_to_human(self.hard_limit))

    def __repr__(self):
        return _repr(self)

class Rule(object):
    def __init__(self, mapirow):
        self.mapirow = mapirow
        name, state = mapirow[PR_RULE_NAME], mapirow[PR_RULE_STATE]
        self.name = unicode(name)
        self.active = bool(state & ST_ENABLED)

    def __unicode__(self):
        return u"Rule('%s')" % self.name

    def __repr__(self):
        return _repr(self)

class Permission(object):
    """Permission class"""

    def __init__(self, mapitable, mapirow, server): # XXX fix args
        self.mapitable = mapitable
        self.mapirow = mapirow
        self.server = server

    @property
    def member(self): # XXX company?
        try:
            return self.server.user(self.server.sa.GetUser(self.mapirow[PR_MEMBER_ENTRYID], MAPI_UNICODE).Username)
        except (NotFoundError, MAPIErrorNotFound):
            return self.server.group(self.server.sa.GetGroup(self.mapirow[PR_MEMBER_ENTRYID], MAPI_UNICODE).Groupname)

    @property
    def rights(self):
        r = []
        for right, name in RIGHT_NAME.items():
            if self.mapirow[PR_MEMBER_RIGHTS] & right:
                r.append(name)
        return r

    @rights.setter
    def rights(self, value):
        r = 0
        for name in value:
            r |= NAME_RIGHT[name]
        self.mapitable.ModifyTable(0, [ROWENTRY(ROW_MODIFY, [SPropValue(PR_MEMBER_ENTRYID, self.mapirow[PR_MEMBER_ENTRYID]), SPropValue(PR_MEMBER_RIGHTS, r)])])

    def __unicode__(self):
        return u"Permission('%s')" % self.member.name

    def __repr__(self):
        return _repr(self)

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
                store_entryid = WrapStoreEntryID(0, b'zarafa6client.dll', store_entryid[:-4])+self.server.pseudo_url+b'\x00'
                mapistore = self.server.mapisession.OpenMsgStore(0, store_entryid, None, 0) # XXX cache
            item = Item()
            item.server = self.server
            item.store = Store(mapiobj=mapistore, server=self.server)
            try:
                item.mapiobj = _openentry_raw(mapistore, entryid.Value, 0)
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
        except Exception as e:
            if self.log:
                self.log.error('could not process change for entryid %s (%r):' % (bin2hex(entryid.Value), props))
                self.log.error(traceback.format_exc(e))
            else:
                traceback.print_exc(e)
            if self.stats:
                self.stats['errors'] += 1
        raise MAPIError(SYNC_E_IGNORE)

    def ImportMessageDeletion(self, flags, entries):
        if self.skip:
            return
        try:
            for entry in entries:
                item = Item()
                item.server = self.server
                item._sourcekey = bin2hex(entry)
                if hasattr(self.importer, 'delete'):
                    self.importer.delete(item, flags)
        except Exception as e:
            if self.log:
                self.log.error('could not process delete for entries: %s' % [bin2hex(entry) for entry in entries])
                self.log.error(traceback.format_exc(e))
            else:
                traceback.print_exc(e)
            if self.stats:
                self.stats['errors'] += 1

    def ImportPerUserReadStateChange(self, states):
        pass

    def UpdateState(self, stream):
        pass

def _daemon_helper(func, service, log):
    try:
        if not service or isinstance(service, Service):
            if isinstance(service, Service): # XXX
                service.log_queue = Queue()
                service.ql = QueueListener(service.log_queue, *service.log.handlers)
                service.ql.start()
            func()
        else:
            func(service)
    finally:
        if isinstance(service, Service) and service.ql: # XXX move queue stuff into Service
            service.ql.stop()
        if log and service:
            log.info('stopping %s', service.name)

def _daemonize(func, options=None, foreground=False, log=None, config=None, service=None):
    uid = gid = None
    working_directory = '/'
    pidfile = None
    if config:
        working_directory = config.get('running_path')
        pidfile = config.get('pid_file')
        if config.get('run_as_user'):
            uid = pwd.getpwnam(config.get('run_as_user')).pw_uid
        if config.get('run_as_group'):
            gid = grp.getgrnam(config.get('run_as_group')).gr_gid
    if not pidfile and service:
        pidfile = "/var/run/kopano/%s.pid" % service.name
    if pidfile:
        pidfile = daemon.pidlockfile.TimeoutPIDLockFile(pidfile, 10)
        oldpid = pidfile.read_pid()
        if oldpid is None:
            # there was no pidfile, remove the lock if it's there
            pidfile.break_lock()
        elif oldpid:
            try:
                cmdline = open('/proc/%u/cmdline' % oldpid).read().split('\0')
            except IOError as error:
                if error.errno != errno.ENOENT:
                    raise
                # errno.ENOENT indicates that no process with pid=oldpid exists, which is ok
                pidfile.break_lock()
    if uid is not None and gid is not None:
        for h in log.handlers:
            if isinstance(h, logging.handlers.WatchedFileHandler):
                os.chown(h.baseFilename, uid, gid)
    if options and options.foreground:
        foreground = options.foreground
        working_directory = os.getcwd()
    with daemon.DaemonContext(
            pidfile=pidfile,
            uid=uid,
            gid=gid,
            working_directory=working_directory,
            files_preserve=[h.stream for h in log.handlers if isinstance(h, logging.handlers.WatchedFileHandler)] if log else None,
            prevent_core=False,
            detach_process=not foreground,
            stdout=sys.stdout,
            stderr=sys.stderr,
        ):
        _daemon_helper(func, service, log)

def _loglevel(options, config):
    if options and getattr(options, 'loglevel', None):
        log_level = options.loglevel
    elif config:
        log_level = config.get('log_level')
    else:
        log_level = 'debug'
    return { # XXX NONE?
        '0': logging.NOTSET,
        '1': logging.CRITICAL,
        '2': logging.ERROR,
        '3': logging.WARNING,
        '4': logging.INFO,
        '5': logging.INFO,
        '6': logging.DEBUG,
        'DEBUG': logging.DEBUG,
        'INFO': logging.INFO,
        'WARNING': logging.WARNING,
        'ERROR': logging.ERROR,
        'CRITICAL': logging.CRITICAL,
    }[log_level.upper()]

def logger(service, options=None, stdout=False, config=None, name=''):
    logger = logging.getLogger(name or service)
    if logger.handlers:
        return logger
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    log_method = 'file'
    log_file = '/var/log/kopano/%s.log' % service
    if config:
        log_method = config.get('log_method') or log_method
        log_file = config.get('log_file') or log_file
    log_level = _loglevel(options, config)
    if name:
        log_file = log_file.replace(service, name) # XXX
    fh = None
    if log_method == 'file' and log_file != '-':
        fh = logging.handlers.WatchedFileHandler(log_file)
    elif log_method == 'syslog':
        fh = logging.handlers.SysLogHandler(address='/dev/log')
    if fh:
        fh.setLevel(log_level)
        fh.setFormatter(formatter)
        logger.addHandler(fh)
    ch = logging.StreamHandler() # XXX via options?
    ch.setLevel(log_level)
    ch.setFormatter(formatter)
    if stdout or (options and options.foreground):
        logger.addHandler(ch)
    logger.setLevel(log_level)
    return logger

def _parse_date(option, opt_str, value, parser):
    setattr(parser.values, option.dest, datetime.datetime.strptime(value, '%Y-%m-%d'))

def _parse_loglevel(option, opt_str, value, parser):
    setattr(parser.values, option.dest, value.upper())

def _parse_str(option, opt_str, value, parser):
    setattr(parser.values, option.dest, _decode(value))

def _parse_list_str(option, opt_str, value, parser):
    getattr(parser.values, option.dest).append(_decode(value))

def parser(options='cskpUPufmvCSlbe', usage=None):
    """
Return OptionParser instance from the standard ``optparse`` module, containing common kopano command-line options

:param options: string containing a char for each desired option, default "cskpUPufmvV"

Available options:

-c, --config: Path to configuration file

-s, --server-socket: Storage server socket address

-k, --sslkey-file: SSL key file

-p, --sslkey-password: SSL key password

-U, --auth-user: Login as user

-P, --auth-pass: Login with password

-C, --company: Run program for specific company

-u, --user: Run program for specific user

-S, --store: Run program for specific store

-f, --folder: Run program for specific folder

-b, --period-begin: Run program for specific period

-e, --period-end: Run program for specific period

-F, --foreground: Run service in foreground

-m, --modify: Enable database modification (python-kopano does not check this!)

-l, --log-level: Set log level (debug, info, warning, error, critical)

-I, --input-dir: Specify input directory

-O, --output-dir: Specify output directory

-v, --verbose: Enable verbose output (python-kopano does not check this!)

-V, --version: Show program version and exit
"""

    parser = optparse.OptionParser(formatter=optparse.IndentedHelpFormatter(max_help_position=42), usage=usage)

    kw_str = {'type': 'str', 'action': 'callback', 'callback': _parse_str}
    kw_date = {'type': 'str', 'action': 'callback', 'callback': _parse_date}
    kw_list_str = {'type': 'str', 'action': 'callback', 'callback': _parse_list_str}

    if 'c' in options: parser.add_option('-c', '--config', dest='config_file', help='load settings from FILE', metavar='FILE', **kw_str)

    if 's' in options: parser.add_option('-s', '--server-socket', dest='server_socket', help='connect to server SOCKET', metavar='SOCKET', **kw_str)
    if 'k' in options: parser.add_option('-k', '--ssl-key', dest='sslkey_file', help='SSL key file', metavar='FILE', **kw_str)
    if 'p' in options: parser.add_option('-p', '--ssl-pass', dest='sslkey_pass', help='SSL key password', metavar='PASS', **kw_str)
    if 'U' in options: parser.add_option('-U', '--auth-user', dest='auth_user', help='login as user', metavar='NAME', **kw_str)
    if 'P' in options: parser.add_option('-P', '--auth-pass', dest='auth_pass', help='login with password', metavar='PASS', **kw_str)

    if 'C' in options: parser.add_option('-C', '--company', dest='companies', default=[], help='run program for specific company', metavar='NAME', **kw_list_str)
    if 'u' in options: parser.add_option('-u', '--user', dest='users', default=[], help='run program for specific user', metavar='NAME', **kw_list_str)
    if 'S' in options: parser.add_option('-S', '--store', dest='stores', default=[], help='run program for specific store', metavar='GUID', **kw_list_str)
    if 'f' in options: parser.add_option('-f', '--folder', dest='folders', default=[], help='run program for specific folder', metavar='NAME', **kw_list_str)

    if 'b' in options: parser.add_option('-b', '--period-begin', dest='period_begin', help='run program for specific period', metavar='DATE', **kw_date)
    if 'e' in options: parser.add_option('-e', '--period-end', dest='period_end', help='run program for specific period', metavar='DATE', **kw_date)

    if 'F' in options: parser.add_option('-F', '--foreground', dest='foreground', action='store_true', help='run program in foreground')

    if 'm' in options: parser.add_option('-m', '--modify', dest='modify', action='store_true', help='enable database modification')
    if 'l' in options: parser.add_option('-l', '--log-level', dest='loglevel', action='callback', default='INFO', type='str', callback=_parse_loglevel, help='set log level (CRITICAL, ERROR, WARNING, INFO, DEBUG)', metavar='LEVEL')
    if 'v' in options: parser.add_option('-v', '--verbose', dest='verbose', action='store_true', help='enable verbose output')
    if 'V' in options: parser.add_option('-V', '--version', dest='version', action='store_true', help='show program version')

    if 'w' in options: parser.add_option('-w', '--worker-processes', dest='worker_processes', help='number of parallel worker processes', metavar='N', type='int')

    if 'I' in options: parser.add_option('-I', '--input-dir', dest='input_dir', help='specify input directory', metavar='PATH', **kw_str)
    if 'O' in options: parser.add_option('-O', '--output-dir', dest='output_dir', help='specify output directory', metavar='PATH', **kw_str)

    return parser

@contextlib.contextmanager # it logs errors, that's all you need to know :-)
def log_exc(log, stats=None):
    """
Context-manager to log any exception in sub-block to given logger instance

:param log: logger instance

Example usage::

    with log_exc(log):
        .. # any exception will be logged when exiting sub-block

"""
    try: yield
    except Exception as e:
        log.error(traceback.format_exc(e))
        if stats:
            stats['errors'] += 1

def _bytes_to_human(b):
    suffixes = ['b', 'kb', 'mb', 'gb', 'tb', 'pb']
    if b == 0: return '0 b'
    i = 0
    len_suffixes = len(suffixes)-1
    while b >= 1024 and i < len_suffixes:
        b /= 1024
        i += 1
    f = ('%.2f' % b).rstrip('0').rstrip('.')
    return '%s %s' % (f, suffixes[i])

def _human_to_bytes(s):
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

class ConfigOption:
    def __init__(self, type_, **kwargs):
        self.type_ = type_
        self.kwargs = kwargs

    def parse(self, key, value):
        return getattr(self, 'parse_'+self.type_)(key, value)

    def parse_string(self, key, value):
        if self.kwargs.get('multiple') == True:
            values = value.split()
        else:
            values = [value]
        for value in values:
            if self.kwargs.get('check_path') is True and not os.path.exists(value): # XXX moved to parse_path
                raise ConfigError("%s: path '%s' does not exist" % (key, value))
            if self.kwargs.get('options') is not None and value not in self.kwargs.get('options'):
                raise ConfigError("%s: '%s' is not a legal value" % (key, value))
        if self.kwargs.get('multiple') == True:
            return values
        else:
            return values[0]

    def parse_path(self, key, value):
        if self.kwargs.get('check', True) and not os.path.exists(value):
            raise ConfigError("%s: path '%s' does not exist" % (key, value))
        return value

    def parse_integer(self, key, value):
        if self.kwargs.get('options') is not None and int(value) not in self.kwargs.get('options'):
            raise ConfigError("%s: '%s' is not a legal value" % (key, value))
        if self.kwargs.get('multiple') == True:
            return [int(x, base=self.kwargs.get('base', 10)) for x in value.split()]
        return int(value, base=self.kwargs.get('base', 10))

    def parse_boolean(self, key, value):
        return {'no': False, 'yes': True, '0': False, '1': True, 'false': False, 'true': True}[value]

    def parse_size(self, key, value):
        return _human_to_bytes(value)

class Config:
    """
Configuration class

:param config: dictionary describing configuration options. TODO describe available options

Example::

    config = Config({
        'some_str': Config.String(default='blah'),
        'number': Config.Integer(),
        'filesize': Config.size(), # understands '5MB' etc
    })

"""
    def __init__(self, config, service=None, options=None, filename=None, log=None):
        self.config = config
        self.service = service
        self.warnings = []
        self.errors = []
        if filename:
            pass
        elif options and getattr(options, 'config_file', None):
            filename = options.config_file
        elif service:
            filename = '/etc/kopano/%s.cfg' % service
        self.data = {}
        if self.config is not None:
            for key, val in self.config.items():
                if 'default' in val.kwargs:
                    self.data[key] = val.kwargs.get('default')
        for line in open(filename):
            line = line.strip().decode('utf-8')
            if not line.startswith('#'):
                pos = line.find('=')
                if pos != -1:
                    key = line[:pos].strip()
                    value = line[pos+1:].strip()
                    if self.config is None:
                        self.data[key] = value
                    elif key in self.config:
                        if self.config[key].type_ == 'ignore':
                            self.data[key] = None
                            self.warnings.append('%s: config option ignored' % key)
                        else:
                            try:
                                self.data[key] = self.config[key].parse(key, value)
                            except ConfigError as e:
                                if service:
                                    self.errors.append(e.message)
                                else:
                                    raise
                    else:
                        msg = "%s: unknown config option" % key
                        if service:
                            self.warnings.append(msg)
                        else:
                            raise ConfigError(msg)
        if self.config is not None:
            for key, val in self.config.items():
                if key not in self.data and val.type_ != 'ignore':
                    msg = "%s: missing in config file" % key
                    if service: # XXX merge
                        self.errors.append(msg)
                    else:
                        raise ConfigError(msg)

    @staticmethod
    def string(**kwargs):
        return ConfigOption(type_='string', **kwargs)

    @staticmethod
    def path(**kwargs):
        return ConfigOption(type_='path', **kwargs)

    @staticmethod
    def boolean(**kwargs):
        return ConfigOption(type_='boolean', **kwargs)

    @staticmethod
    def integer(**kwargs):
        return ConfigOption(type_='integer', **kwargs)

    @staticmethod
    def size(**kwargs):
        return ConfigOption(type_='size', **kwargs)

    @staticmethod
    def ignore(**kwargs):
        return ConfigOption(type_='ignore', **kwargs)

    def get(self, x, default=None):
        return self.data.get(x, default)

    def __getitem__(self, x):
        return self.data[x]

CONFIG = {
    'log_method': Config.string(options=['file', 'syslog'], default='file'),
    'log_level': Config.string(options=[str(i) for i in range(7)] + ['info', 'debug', 'warning', 'error', 'critical'], default='info'),
    'log_file': Config.string(default=None),
    'log_timestamp': Config.integer(options=[0,1], default=1),
    'pid_file': Config.string(default=None),
    'run_as_user': Config.string(default=None),
    'run_as_group': Config.string(default=None),
    'running_path': Config.string(check_path=True, default='/var/lib/kopano'),
    'server_socket': Config.string(default=None),
    'sslkey_file': Config.string(default=None),
    'sslkey_pass': Config.string(default=None),
    'worker_processes': Config.integer(default=1),
}

# log-to-queue handler copied from Vinay Sajip
class QueueHandler(logging.Handler):
    def __init__(self, queue):
        logging.Handler.__init__(self)
        self.queue = queue

    def enqueue(self, record):
        self.queue.put_nowait(record)

    def prepare(self, record):
        self.format(record)
        record.msg, record.args, record.exc_info = record.message, None, None
        return record

    def emit(self, record):
        try:
            self.enqueue(self.prepare(record))
        except (KeyboardInterrupt, SystemExit):
            raise
        except:
            self.handleError(record)

# log-to-queue listener copied from Vinay Sajip
class QueueListener(object):
    _sentinel = None

    def __init__(self, queue, *handlers):
        self.queue = queue
        self.handlers = handlers
        self._stop = threading.Event()
        self._thread = None

    def dequeue(self, block):
        return self.queue.get(block)

    def start(self):
        self._thread = t = threading.Thread(target=self._monitor)
        t.setDaemon(True)
        t.start()

    def prepare(self, record):
        return record

    def handle(self, record):
        record = self.prepare(record)
        for handler in self.handlers:
            handler.handle(record)

    def _monitor(self):
        q = self.queue
        has_task_done = hasattr(q, 'task_done')
        while not self._stop.isSet():
            try:
                record = self.dequeue(True)
                if record is self._sentinel:
                    break
                self.handle(record)
                if has_task_done:
                    q.task_done()
            except (Empty, EOFError):
                pass
        # There might still be records in the queue.
        while True:
            try:
                record = self.dequeue(False)
                if record is self._sentinel:
                    break
                self.handle(record)
                if has_task_done:
                    q.task_done()
            except (Empty, EOFError):
                break

    def stop(self):
        self._stop.set()
        self.queue.put_nowait(self._sentinel)
        self._thread.join()
        self._thread = None

class Service:
    """
Encapsulates everything to create a simple service, such as:

- Locating and parsing a configuration file
- Performing logging, as specifified in the configuration file
- Handling common command-line options (-c, -F)
- Daemonization (if no -F specified)

:param name: name of the service; if for example 'search', the configuration file should be called ``/etc/kopano/search.cfg`` or passed with -c
:param config: :class:`Configuration <Config>` to use
:param options: OptionParser instance to get settings from (see :func:`parser`)

"""

    def __init__(self, name, config=None, options=None, args=None, logname=None, **kwargs):
        self.name = name
        self.__dict__.update(kwargs)
        if not options:
            options, args = parser('cskpUPufmvVFw').parse_args()
            args = [_decode(arg) for arg in args]
        self.options, self.args = options, args
        self.name = name
        self.logname = logname
        self.ql = None
        config2 = CONFIG.copy()
        if config:
            config2.update(config)
        if getattr(options, 'config_file', None):
            options.config_file = os.path.abspath(options.config_file) # XXX useful during testing. could be generalized with optparse callback?
        if getattr(options, 'service', True) == False:
            options.foreground = True
        self.config = Config(config2, service=name, options=options)
        self.config.data['server_socket'] = os.getenv("KOPANO_SOCKET") or self.config.data['server_socket']
        if getattr(options, 'worker_processes', None):
            self.config.data['worker_processes'] = options.worker_processes
        self.log = logger(self.logname or self.name, options=self.options, config=self.config) # check that this works here or daemon may die silently XXX check run_as_user..?
        for msg in self.config.warnings:
            self.log.warn(msg)
        if self.config.errors:
            for msg in self.config.errors:
                self.log.error(msg)
            sys.exit(1)
        self.stats = collections.defaultdict(int, {'errors': 0})
        self._server = None

    def main(self):
        raise Error('Service.main not implemented')

    @property
    def server(self):
        if self._server is None:
            self._server = Server(options=self.options, config=self.config.data, log=self.log, service=self)
        return self._server

    def start(self):
        for sig in (signal.SIGTERM, signal.SIGINT):
            signal.signal(sig, lambda *args: sys.exit(-sig))
        signal.signal(signal.SIGHUP, signal.SIG_IGN) # XXX long term, reload config?

        self.log.info('starting %s', self.logname or self.name)
        with log_exc(self.log):
            if getattr(self.options, 'service', True): # do not run-as-service (eg backup)
                _daemonize(self.main, options=self.options, log=self.log, config=self.config, service=self)
            else:
                _daemon_helper(self.main, self, self.log)

class Worker(Process):
    def __init__(self, service, name, **kwargs):
        Process.__init__(self)
        self.daemon = True
        self.name = name
        self.service = service
        self.__dict__.update(kwargs)
        self.log = logging.getLogger(name=self.name)
        if not self.log.handlers:
            loglevel = _loglevel(service.options, service.config)
            formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
            qh = QueueHandler(service.log_queue)
            qh.setFormatter(formatter)
            qh.setLevel(loglevel)
            self.log.addHandler(qh)
            self.log.setLevel(loglevel)

    def main(self):
        raise Error('Worker.main not implemented')

    def run(self):
        self.service._server = None # do not re-use "forked" session
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, lambda *args: sys.exit(0))
        with log_exc(self.log):
            self.main()

class _ZSocket: # XXX megh, double wrapper
    def __init__(self, addr, ssl_key, ssl_cert):
        self.ssl_key = ssl_key
        self.ssl_cert = ssl_cert
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.bind(addr)
        self.s.listen(socket.SOMAXCONN)

    def accept(self):
        newsocket, fromaddr = self.s.accept()
        connstream = ssl.wrap_socket(newsocket, server_side=True, keyfile=self.ssl_key, certfile=self.ssl_cert)
        return connstream, fromaddr


def server_socket(addr, ssl_key=None, ssl_cert=None, log=None): # XXX https, merge code with client_socket
    if addr.startswith('file://'):
        addr2 = addr.replace('file://', '')
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        os.system('rm -f %s' % addr2)
        s.bind(addr2)
        s.listen(socket.SOMAXCONN)
    elif addr.startswith('https://'):
        addr2 = addr.replace('https://', '').split(':')
        addr2 = (addr2[0], int(addr2[1]))
        s = _ZSocket(addr2, ssl_key=ssl_key, ssl_cert=ssl_cert)
    else:
        addr2 = addr.replace('http://', '').split(':')
        addr2 = (addr2[0], int(addr2[1]))
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.bind(addr2)
        s.listen(socket.SOMAXCONN)
    if log:
        log.info('listening on socket %s', addr)
    return s

def client_socket(addr, ssl_cert=None, log=None):
    if addr.startswith('file://'):
        addr2 = addr.replace('file://', '')
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    elif addr.startswith('https://'):
        addr2 = addr.replace('https://', '').split(':')
        addr2 = (addr2[0], int(addr2[1]))
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s = ssl.wrap_socket(s, ca_certs=ssl_cert, cert_reqs=ssl.CERT_REQUIRED)
    else:
        addr2 = addr.replace('http://', '').split(':')
        addr2 = (addr2[0], int(addr2[1]))
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(addr2)
    return s
