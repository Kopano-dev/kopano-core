# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import binascii
import os
try:
    import cPickle as pickle
except ImportError:
    import _pickle as pickle
import struct
import time

from MAPI import (
    WrapCompressedRTFStream, PT_UNICODE, ROW_ADD, MAPI_MODIFY,
    KEEP_OPEN_READWRITE,
)
from MAPI.Defs import (
    PROP_TYPE
)
from MAPI.Tags import (
    PR_RTF_COMPRESSED, PR_ACL_TABLE, PR_MEMBER_ENTRYID, PR_MEMBER_RIGHTS
)
from MAPI.Tags import (
    IID_IStream, IID_IECMessageRaw, IID_IExchangeModifyTable
)
from MAPI.Struct import (
    MAPIErrorNotFound, MAPIErrorInterfaceNotSupported, SPropValue, ROWENTRY,
    MAPIErrorNoAccess, MAPIErrorDiskError
)

TESTING = False
if os.getenv('PYKO_TESTING'): # env variable used in testset
    TESTING = True

MAX_SAVE_RETRIES = int(os.getenv('PYKO_MAPI_SAVE_MAX_RETRIES', 3))

from .compat import bdec as _bdec
from .errors import Error, NotFoundError, ArgumentError

from . import table as _table
from . import permission as _permission
from . import user as _user
from . import group as _group

def pickle_loads(s):
    return pickle.loads(s, encoding='bytes')

def pickle_dumps(s):
    return pickle.dumps(s, protocol=2)

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

# avoid underwater action for archived items
def _openentry_helper(mapistore, entryid, flags):
    try:
        return mapistore.OpenEntry(entryid, IID_IECMessageRaw, flags)
    except MAPIErrorInterfaceNotSupported:
        return mapistore.OpenEntry(entryid, None, flags)

# try to open read/write, falling back to read-only
def openentry_raw(mapistore, entryid, flags):
    try:
        return _openentry_helper(mapistore, entryid, flags | MAPI_MODIFY)
    except MAPIErrorNoAccess:
        return _openentry_helper(mapistore, entryid, flags)

def unpack_short(s, pos):
    return struct.unpack_from('<H', s, pos)[0]

def unpack_long(s, pos):
    return struct.unpack_from('<L', s, pos)[0]

def pack_short(i):
    return struct.pack('<H', i)

def pack_long(i):
    return struct.pack('<L', i)

def rectime_to_unixtime(t):
    return (t - 194074560) * 60

def unixtime_to_rectime(t):
    return int(t / 60) + 194074560

def permissions(obj):
        try:
            acl_table = obj.mapiobj.OpenProperty(
                PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
        except MAPIErrorNotFound:
            return
        table = _table.Table(
            obj.server,
            obj.mapiobj,
            acl_table.GetTable(0),
            PR_ACL_TABLE,
        )
        for row in table.dict_rows():
            yield _permission.Permission(acl_table, row, obj.server)

def permission(obj, member, create):
        for permission in obj.permissions():
            if permission.member == member:
                return permission
        if create:
            acl_table = obj.mapiobj.OpenProperty(
                PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)

            if isinstance(member, _user.User): # TODO *.id_ or something..?
                memberid = member.userid
            elif isinstance(member, _group.Group):
                memberid = member.groupid
            else:
                memberid = member.companyid

            props = [
                SPropValue(PR_MEMBER_ENTRYID, _bdec(memberid)),
                SPropValue(PR_MEMBER_RIGHTS, 0)
            ]
            acl_table.ModifyTable(0, [ROWENTRY(ROW_ADD, props)])
            return obj.permission(member)
        else:
            raise NotFoundError("no permission entry for '%s'" % member.name)

def bytes_to_human(b):
    suffixes = ['b', 'kb', 'mb', 'gb', 'tb', 'pb']
    if b == 0: return '0 b'
    i = 0
    len_suffixes = len(suffixes) - 1
    while b >= 1024 and i < len_suffixes:
        b //= 1024
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
    try:
        num = float(num)
    except ValueError:
        raise ArgumentError('invalid size: %r' % init)
    letter = s.strip()
    for sset in [
        ('b', 'k', 'm', 'g', 't', 'p', 'e', 'z', 'y'),
        ('b', 'kb', 'mb', 'gb', 'tb', 'pb', 'eb', 'zb', 'yb'),
        ('b', 'kib', 'mib', 'gib', 'tib', 'pib', 'eib', 'zib', 'yib')
    ]:
        if letter in sset:
            break
    else:
        raise ArgumentError('invalid size: %r' % init)
    prefix = {sset[0]: 1}
    for i, s in enumerate(sset[1:]):
        prefix[s] = 1 << (i + 1) * 10
    return int(num * prefix[letter])

def arg_objects(arg, supported_classes, method_name):
    if isinstance(arg, supported_classes):
        objects = [arg]
    else:
        try:
            objects = list(arg)
        except TypeError:
            raise ArgumentError('invalid argument to %s' % method_name)

    if [o for o in objects if not isinstance(o, supported_classes)]:
        raise ArgumentError('invalid argument to %s' % method_name)
    return objects

def _bdec_eid(entryid):
    try:
        return _bdec(entryid)
    except (TypeError, AttributeError, binascii.Error):
        raise ArgumentError("invalid entryid: %r" % entryid) from None

def _save(mapiobj):
    # retry on deadlock or other temporary issue
    t = 0.1
    retry = 0
    while True:
        try:
            if TESTING and os.getenv('PYKO_TEST_DISK_ERROR'): # test coverage
                raise MAPIErrorDiskError()
            mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
            break
        except MAPIErrorDiskError:
            if retry >= MAX_SAVE_RETRIES:
                raise Error('could not save object')
            else:
                retry += 1
                time.sleep(t)
