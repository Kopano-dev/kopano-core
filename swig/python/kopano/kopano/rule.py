"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from MAPI.Util import *

from .utils import repr as _repr

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

