"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import datetime
import struct
import sys

from MAPI import (
    WrapCompressedRTFStream, PT_UNICODE, ROW_ADD, MAPI_MODIFY,
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
    MAPIErrorNoAccess
)

from .compat import unhex as _unhex, hex as _hex
from .errors import Error, NotFoundError

if sys.hexversion >= 0x03000000:
    from . import table as _table
    from . import permission as _permission
    from . import user as _user
    from . import group as _group
else:
    import table as _table
    import permission as _permission
    import user as _user
    import group as _group

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

def _openentry_helper(mapistore, entryid, flags): # avoid underwater action for archived items
    try:
        return mapistore.OpenEntry(entryid, IID_IECMessageRaw, flags)
    except MAPIErrorInterfaceNotSupported:
        return mapistore.OpenEntry(entryid, None, flags)

def openentry_raw(mapistore, entryid, flags): # try to open read/write, falling back to read-only
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
            acl_table = obj.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
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
            acl_table = obj.mapiobj.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, 0)
            if isinstance(member, _user.User): # XXX *.id_ or something..?
                memberid = member.userid
            elif isinstance(member, _group.Group):
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
    len_suffixes = len(suffixes) - 1
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
    prefix = {sset[0]: 1}
    for i, s in enumerate(sset[1:]):
        prefix[s] = 1 << (i + 1) * 10
    return int(num * prefix[letter])

def _in_dst(date, dststartmonth, dststartweek, dststarthour, dstendmonth, dstendweek, dstendhour):
    dststart = datetime.datetime(date.year, dststartmonth, 1) + \
        datetime.timedelta(seconds=dststartweek*7*24*60*60 + dststarthour*60*60)

    dstend = datetime.datetime(date.year, dstendmonth, 1) + \
        datetime.timedelta(seconds=dstendweek*7*24*60*60 + dstendhour*60*60)

    if dststart <= dstend:
        if dststart < date < dstend:
            return True
    else:
        if data < dstend or data > dststart:
            return True

    return False

# XXX check doc for exact format, check php version
def _get_timezone(date, tz_data, align_dst=False):
    if tz_data is None:
        return 0

    timezone, _, timezonedst, _, dstendmonth, dstendweek, dstendhour, _, _, _, dststartmonth, dststartweek, dststarthour, _, _ = struct.unpack('<lllllHHllHlHHlH', tz_data)

    dst = _in_dst(date, dststartmonth, dststartweek, dststarthour, dstendmonth, dstendweek, dstendhour)

    # TODO use DST-aware datetimes?
    if align_dst and _in_dst(datetime.datetime.now(),
       dststartmonth, dststartweek, dststarthour,
       dstendmonth, dstendweek, dstendhour) != dst:
        dst = not dst

    if dst:
        return timezone + timezonedst
    else:
        return timezone

def _from_gmt(date, tz_data):
    return date - datetime.timedelta(minutes=_get_timezone(date, tz_data))

def _to_gmt(date, tz_data, align_dst=False):
    return date + datetime.timedelta(minutes=_get_timezone(date, tz_data, align_dst=align_dst))

def arg_objects(arg, supported_classes, method_name):
    if isinstance(arg, supported_classes):
        objects = [arg]
    else:
        try:
            objects = list(arg)
        except TypeError:
            raise Error('invalid argument to %s' % method_name)

    if [o for o in objects if not isinstance(o, supported_classes)]:
        raise Error('invalid argument to %s' % method_name)
    return objects

