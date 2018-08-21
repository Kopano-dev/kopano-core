# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano.

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI import MAPI_UNICODE, ROW_MODIFY
from MAPI.Struct import MAPIErrorNotFound, ROWENTRY, SPropValue
from MAPI.Tags import PR_MEMBER_ENTRYID, PR_MEMBER_RIGHTS, PR_MEMBER_ID

from .compat import repr as _repr
from .defs import RIGHT_NAME, NAME_RIGHT
from .errors import NotFoundError

class Permission(object):
    """Permission class"""

    def __init__(self, mapitable, mapirow, server): # XXX fix args
        self.mapitable = mapitable
        self.mapirow = mapirow
        self.server = server

    @property
    def member(self): # XXX company?
        """:class:`User <User>` or :class:`group <Group>` given specific rights."""
        try:
            return self.server.user(self.server.sa.GetUser(self.mapirow[PR_MEMBER_ENTRYID], MAPI_UNICODE).Username)
        except (NotFoundError, MAPIErrorNotFound):
            return self.server.group(self.server.sa.GetGroup(self.mapirow[PR_MEMBER_ENTRYID], MAPI_UNICODE).Groupname)

    @property
    def rights(self):
        """Rights given to member.

        Possible rights:

        read_items, create_items, create_subfolders, edit_own, edit_all,
        delete_own, delete_all, folder_owner, folder_contact, folder_visible
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

        self.mapitable.ModifyTable(0, [ROWENTRY(ROW_MODIFY, [SPropValue(PR_MEMBER_ID, self.mapirow[PR_MEMBER_ID]), SPropValue(PR_MEMBER_ENTRYID, self.mapirow[PR_MEMBER_ENTRYID]), SPropValue(PR_MEMBER_RIGHTS, r)])]) # PR_MEMBER_ID needed, or it becomes ROW_ADD

    def __unicode__(self):
        return u"Permission('%s')" % self.member.name

    def __repr__(self):
        return _repr(self)
