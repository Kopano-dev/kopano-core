# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano.

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import sys

from MAPI import (
    MAPI_UNICODE, ROW_MODIFY, ROW_ADD, MAPI_MODIFY,
)
from MAPI.Struct import (
    MAPIErrorNotFound, ROWENTRY, SPropValue,
)
from MAPI.Tags import (
    PR_MEMBER_ENTRYID, PR_MEMBER_RIGHTS, PR_MEMBER_ID, PR_ACL_TABLE,
    IID_IExchangeModifyTable,
)

from .compat import (
    bdec as _bdec, benc as _benc, repr as _repr
)
from .defs import RIGHT_NAME, NAME_RIGHT
from .errors import NotFoundError
from .log import log_exc

try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__ + '.utils']

def _permissions_dumps(obj, stats=None):
    server = obj.server
    log = server.log

    rows = []
    with log_exc(log, stats):
        acl_table = obj.mapiobj.OpenProperty(PR_ACL_TABLE,
            IID_IExchangeModifyTable, 0, 0)
        table = acl_table.GetTable(0)
        for row in table.QueryRows(-1,0):
            entryid = row[1].Value
            try:
                row[1].Value = (b'user', server.sa.GetUser(entryid,
                    MAPI_UNICODE).Username)
            except MAPIErrorNotFound:
                try:
                    row[1].Value = (b'group', server.sa.GetGroup(entryid,
                        MAPI_UNICODE).Groupname)
                except MAPIErrorNotFound:
                    log.warning("skipping access control entry for unknown \
user/group %s", _benc(entryid))
                    continue
            rows.append(row)
    return _utils.pickle_dumps({
        b'data': rows
    })

def _permissions_loads(obj, data, stats=None):
    server = obj.server
    log = server.log

    with log_exc(log, stats):
        data = _utils.pickle_loads(data)
        if isinstance(data, dict):
            data = data[b'data']
        rows = []
        for row in data:
            try:
                member_type, value = row[1].Value
                if member_type == b'user':
                    entryid = server.user(value).userid
                else:
                    entryid = server.group(value).groupid
                row[1].Value = _bdec(entryid)
                rows.append(row)
            except kopano.NotFoundError:
                log.warning("skipping access control entry for unknown \
user/group '%s'", value)
        acltab = obj.mapiobj.OpenProperty(PR_ACL_TABLE,
            IID_IExchangeModifyTable, 0, MAPI_MODIFY)
        acltab.ModifyTable(0, [ROWENTRY(ROW_ADD, row) for row in rows])

class Permission(object):
    """Permission class

    A permission instance combines a store or folder with a user or group and
    a set of permissions.

    Permissions for a given folder are resolved by following the parent
    chain (and ultimately store), until there is a match on user or group.
    """

    def __init__(self, mapitable, mapirow, server): # TODO fix args
        self.mapitable = mapitable
        self.mapirow = mapirow
        self.server = server

    @property
    def member(self): # TODO companies in addition to users/groups?
        """The associated :class:`User <User>` or :class:`group <Group>`."""
        try:
            return self.server.user(self.server.sa.GetUser(
                self.mapirow[PR_MEMBER_ENTRYID], MAPI_UNICODE).Username)
        except (NotFoundError, MAPIErrorNotFound):
            return self.server.group(self.server.sa.GetGroup(
                self.mapirow[PR_MEMBER_ENTRYID], MAPI_UNICODE).Groupname)

    @property
    def rights(self): # TODO add def roles!!
        """The rights given to the associated member.

        Possible rights:

        *read_items*
        *create_items*
        *create_subfolders*
        *edit_own*,
        *edit_all*
        *delete_own*
        *delete_all*
        *folder_owner*
        *folder_contact*
        *folder_visible*
        """
        r = []
        for right, name in RIGHT_NAME.items():
            if self.mapirow[PR_MEMBER_RIGHTS] & right:
                r.append(name)
        return r

    @rights.setter
    def rights(self, value):
        r = 0
        for name in value:
            try:
                r |= NAME_RIGHT[name]
            except KeyError:
                raise NotFoundError("no such right: '%s'" % name)

        self.mapitable.ModifyTable(0, [ROWENTRY(ROW_MODIFY,
            [SPropValue(PR_MEMBER_ID, self.mapirow[PR_MEMBER_ID]),
             SPropValue(PR_MEMBER_ENTRYID, self.mapirow[PR_MEMBER_ENTRYID]),
             SPropValue(PR_MEMBER_RIGHTS, r)]
        )]) # PR_MEMBER_ID needed, or it becomes ROW_ADD

    def __unicode__(self):
        return "Permission('%s')" % self.member.name

    def __repr__(self):
        return _repr(self)
