"""
Part of the high-level python bindings for Kopano

Copyright 2018 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI.Struct import MAPIErrorNotFound
from MAPI.Util import TestRestriction

from MAPI.Tags import (
    PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_EMAIL_ADDRESS_W, PR_ENTRYID,
    PR_SEARCH_KEY,
)

from .address import Address
from .compat import repr as _repr

class Attendee(object):
    """Attendee class"""

    def __init__(self, server, mapirow):
        self.server = server
        self.mapirow = mapirow
        self.row = dict([(x.proptag, x) for x in mapirow])

    @property
    def address(self):
        args = [self.row[p].value if p in self.row else None for p in
            (PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_EMAIL_ADDRESS_W, PR_ENTRYID, PR_SEARCH_KEY)]

        return Address(self.server, *args, props=self.mapirow)

    def __unicode__(self):
        return u'Attendee()'

    def __repr__(self):
        return _repr(self)

