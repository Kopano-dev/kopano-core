"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)

Deprecated
"""

import warnings

from .compat import repr as _repr
from .errors import _DeprecationWarning

class Body(object):
    """Item Body class"""

    def __init__(self, item):
        warnings.warn('class Body is deprecated', _DeprecationWarning)
        self.item = item

    @property
    def text(self):
        """ Plain text representation """

        warnings.warn('Body.text is deprecated', _DeprecationWarning)
        return self.item.text

    @text.setter
    def text(self, x):
        warnings.warn('Body.text is deprecated', _DeprecationWarning)
        self.item.text = x

    @property
    def html(self):
        """ HTML representation """

        warnings.warn('Body.html is deprecated', _DeprecationWarning)
        return self.item.html

    @html.setter
    def html(self, x):
        warnings.warn('Body.html is deprecated', _DeprecationWarning)
        self.item.html = x

    @property
    def rtf(self):
        """ RTF representation """

        warnings.warn('Body.rtf is deprecated', _DeprecationWarning)
        return self.item.rtf

    @property
    def type_(self):
        """ original body type: 'text', 'html', 'rtf' or *None* if it
        cannot be determined """

        warnings.warn('Body.type_ is deprecated', _DeprecationWarning)
        return self.item.body_type

    def __unicode__(self):
        return u'Body()'

    def __repr__(self):
        return _repr(self)
