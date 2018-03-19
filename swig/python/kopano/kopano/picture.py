"""
Part of the high-level python bindings for Kopano

Copyright 2018 - Kopano and its licensors (see LICENSE file for details)
"""

from .compat import repr as _repr

class Picture(object):
    """Picture class"""

    def __init__(self, data, name=u'', mimetype=u''):
        self.data = data
        self.name = name
        self.mimetype = mimetype

    def __unicode__(self):
        return u'Picture(%s)' % (self.name or u'')

    def __repr__(self):
        return _repr(self)
