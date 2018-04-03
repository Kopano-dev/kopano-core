"""
Part of the high-level python bindings for Kopano

Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI.Struct import MAPIErrorNotFound
from MAPI.Util import TestRestriction

from .compat import repr as _repr

class Restriction(object):
    """Restriction class"""

    def __init__(self, mapiobj=None):
        self.mapiobj = mapiobj

    def match(self, item):
        try:
            TestRestriction(self.mapiobj, item.mapiobj)
            return True
        except MAPIErrorNotFound:
            return False

    def __unicode__(self):
        return u'Restriction()'

    def __repr__(self):
        return _repr(self)
