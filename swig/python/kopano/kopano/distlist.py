# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Part of the high-level python bindings for Kopano

Copyright 2018 - Kopano and its licensors (see LICENSE file for details)
"""

import codecs
import sys

from .address import Address
from .compat import repr as _repr
from .pidlid import (
    PidLidDistributionListMembers,
    PidLidDistributionListOneOffMembers
)

if sys.hexversion >= 0x03000000:
    def bval(x):
        return x

else: # pragma: no cover
    def bval(x):
        return ord(x)

WRAPPED_ENTRYID_PREFIX = codecs.decode('00000000C091ADD3519DCF11A4A900AA0047FAA4', 'hex')
WRAPPED_EID_TYPE_MASK = 0xf
WRAPPED_EID_TYPE_CONTACT = 3
WRAPPED_EID_TYPE_PERSONAL_DISTLIST = 4

class DistList(object):
    """DistList class"""

    def __init__(self, item):
        self.item = item

    def members(self, expand=True):
        members = self.item.get(PidLidDistributionListMembers)
        oneoffs = self.item.get(PidLidDistributionListOneOffMembers)

        for i, (member, oneoff) in enumerate(zip(members, oneoffs)):
            pos = len(WRAPPED_ENTRYID_PREFIX)
            prefix, flags, rest = member[:pos], bval(member[pos]), member[pos+1:]

            if (prefix == WRAPPED_ENTRYID_PREFIX and \
                (flags & WRAPPED_EID_TYPE_MASK) == (WRAPPED_EID_TYPE_PERSONAL_DISTLIST)):

                item = self.item.store.item(codecs.encode(rest, 'hex'))

                if expand:
                    for member2 in item.distlist.members(expand=True):
                        yield member2
                else:
                    yield DistList(item)

            else:
                yield Address(server=self.item.server, oneoff=oneoff)

    # TODO add, delete members

    def __unicode__(self):
        return u'DistList(%s)' % self.item.name

    def __repr__(self):
        return _repr(self)
