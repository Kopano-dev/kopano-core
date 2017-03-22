"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import sys

from MAPI.Tags import (
    PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED
)
from MAPI.Struct import MAPIErrorNotFound

from .compat import repr as _repr

if sys.hexversion >= 0x03000000:
    from . import utils as _utils
    from . import prop as _prop
else:
    import utils as _utils
    import prop as _prop

class Body:
    """Item Body class"""

    # XXX XXX setters!

    def __init__(self, mapiitem):
        self.mapiitem = mapiitem

    @property
    def text(self):
        """ Plain text representation """

        try:
            mapiitem = self.mapiitem._arch_item # XXX server already goes 'underwater'.. check details
            return _utils.stream(mapiitem, PR_BODY_W) # under windows them be utf-16le?
        except MAPIErrorNotFound:
            return u''

    @property
    def html(self):
        """ HTML representation """

        try:
            mapiitem = self.mapiitem._arch_item
            return _utils.stream(mapiitem, PR_HTML)
        except MAPIErrorNotFound:
            return ''

    @property
    def rtf(self):
        """ RTF representation """

        try:
            mapiitem = self.mapiitem._arch_item
            return _utils.stream(mapiitem, PR_RTF_COMPRESSED)
        except MAPIErrorNotFound:
            return ''

    @property
    def type_(self):
        """ original body type: 'text', 'html', 'rtf' or None if it cannot be determined """
        tag = _prop.bestbody(self.mapiitem.mapiobj)
        if tag == PR_BODY_W:
            return 'text'
        elif tag == PR_HTML:
            return 'html'
        elif tag == PR_RTF_COMPRESSED:
            return 'rtf'

    def __unicode__(self):
        return u'Body()'

    def __repr__(self):
        return _repr(self)
