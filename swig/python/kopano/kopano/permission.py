"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI.Util import *

from .defs import *
from .errors import *

from utils import repr as _repr

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
        self.mapitable.ModifyTable(0, [ROWENTRY(ROW_MODIFY, [SPropValue(PR_MEMBER_ID, self.mapirow[PR_MEMBER_ID]), SPropValue(PR_MEMBER_ENTRYID, self.mapirow[PR_MEMBER_ENTRYID]), SPropValue(PR_MEMBER_RIGHTS, r)])]) # PR_MEMBER_ID needed, or it becomes ROW_ADD

    def __unicode__(self):
        return u"Permission('%s')" % self.member.name

    def __repr__(self):
        return _repr(self)
