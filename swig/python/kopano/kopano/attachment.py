"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import sys

from MAPI.Tags import (
    PR_EC_HIERARCHYID, PR_ATTACH_NUM, PR_ATTACH_MIME_TAG_W,
    PR_ATTACH_LONG_FILENAME_W, PR_ATTACH_SIZE, PR_ATTACH_DATA_BIN
)
from MAPI.Defs import HrGetOneProp
from MAPI.Struct import MAPIErrorNotFound

from .compat import repr as _repr

if sys.hexversion >= 0x03000000:
    from . import utils as _utils
else:
    import utils as _utils

class Attachment(object):
    """Attachment class"""

    def __init__(self, mapiobj):
        self.mapiobj = mapiobj
        self._data = None

    @property
    def hierarchyid(self):
        return self.prop(PR_EC_HIERARCHYID).value

    @property
    def number(self):
        try:
            return HrGetOneProp(self.mapiobj, PR_ATTACH_NUM).Value
        except MAPIErrorNotFound:
            return 0

    @property
    def mimetype(self):
        """ Mime-type or *None* if not found """

        try:
            return HrGetOneProp(self.mapiobj, PR_ATTACH_MIME_TAG_W).Value
        except MAPIErrorNotFound:
            pass

    @property
    def filename(self):
        """ Filename or *None* if not found """

        try:
            return HrGetOneProp(self.mapiobj, PR_ATTACH_LONG_FILENAME_W).Value
        except MAPIErrorNotFound:
            pass

    @property
    def size(self):
        """ Size """
        # XXX size of the attachment object, so more than just the attachment data
        # XXX (useful when calculating store size, for example.. sounds interesting to fix here)
        try:
            return int(HrGetOneProp(self.mapiobj, PR_ATTACH_SIZE).Value)
        except MAPIErrorNotFound:
            return 0 # XXX

    def __len__(self):
        return self.size

    @property
    def data(self):
        """ Binary data """

        if self._data is None:
            self._data = _utils.stream(self.mapiobj, PR_ATTACH_DATA_BIN)
        return self._data

    # file-like behaviour
    def read(self):
        return self.data

    @property
    def name(self):
        return self.filename

    def prop(self, proptag):
        return _utils.prop(self, self.mapiobj, proptag)

    def props(self):
        return _utils.props(self.mapiobj)

    def __unicode__(self):
        return u'Attachment("%s")' % self.name

    def __repr__(self):
        return _repr(self)
