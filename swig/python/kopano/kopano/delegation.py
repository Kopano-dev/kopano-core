"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from .compat import repr as _repr
from .defs import *

class Delegation(object):
    """Delegation class"""

    def __init__(self, store, user):
        self.store = store
        self.user = user

    @property
    def see_private(self):
        fbmsg, (entryids, names, flagss) = self.store._fbmsg_delgs()
        pos = entryids.Value.index(self.user.userid.decode('hex'))

        return bool(flagss.Value[pos] & 1)

    @see_private.setter
    def see_private(self, b):
        fbmsg, (entryids, names, flagss) = self.store._fbmsg_delgs()
        pos = entryids.Value.index(self.user.userid.decode('hex'))

        if b:
            flagss.Value[pos] |= 1
        else:
            flagss.Value[pos] &= ~1

        fbmsg.SetProps([flagss])
        fbmsg.SaveChanges(KEEP_OPEN_READWRITE)

    def _delete(self):
        fbmsg, (entryids, names, flagss) = self.store._fbmsg_delgs()
        pos = entryids.Value.index(self.user.userid.decode('hex'))

        del entryids.Value[pos]
        del names.Value[pos]
        del flagss.Value[pos]

        fbmsg.SetProps([entryids, names, flagss])
        fbmsg.SaveChanges(KEEP_OPEN_READWRITE)

    def __unicode__(self):
        return u"Delegation('%s')" % self.user.name

    def __repr__(self):
        return _repr(self)

