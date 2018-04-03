"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

import sys

from MAPI import (
    MAPI_MODIFY, MAPI_DEFERRED_ERRORS,
)

from MAPI.Tags import (
    PR_EC_HIERARCHYID, PR_ATTACH_NUM, PR_ATTACH_MIME_TAG_W, PR_RECORD_KEY,
    PR_ATTACH_LONG_FILENAME_W, PR_ATTACH_SIZE, PR_ATTACH_DATA_BIN, PR_ENTRYID,
    PR_LAST_MODIFICATION_TIME, IID_IAttachment, PR_ATTACH_METHOD,
    ATTACH_EMBEDDED_MSG, PR_ATTACH_DATA_OBJ, IID_IMessage,
)
from MAPI.Defs import HrGetOneProp
from MAPI.Struct import (
    MAPIErrorNotFound, MAPIErrorNoAccess,
)

if sys.hexversion >= 0x03000000:
    try:
        from . import item as _item
    except ImportError:
        _item = sys.modules[__package__ + '.item']
else:
    import item as _item

from .properties import Properties

from .compat import (
    benc as _benc, fake_unicode as _unicode,
)

if sys.hexversion >= 0x03000000:
    try:
        from . import utils as _utils
    except ImportError:
        _utils = sys.modules[__package__ + '.utils']
else:
    import utils as _utils

class Attachment(Properties):
    """Attachment class"""

    def __init__(self, parent, mapiitem=None, entryid=None, mapiobj=None):
        self.parent = parent
        self._mapiitem = mapiitem
        self._entryid = entryid
        self._mapiobj = mapiobj
        self._data = None

    @property
    def mapiobj(self):
        if self._mapiobj:
            return self._mapiobj

        self._mapiobj = self._mapiitem.OpenAttach(
            self._entryid, IID_IAttachment, 0
        )
        return self._mapiobj

    @property
    def entryid(self):
        return _benc(HrGetOneProp(self._mapiitem, PR_ENTRYID).Value) + _benc(self[PR_RECORD_KEY])

    @mapiobj.setter
    def mapiobj(self, mapiobj):
        self._mapiobj = mapiobj

    @property
    def hierarchyid(self):
        return self.prop(PR_EC_HIERARCHYID).value

    @property
    def number(self):
        return self.get(PR_ATTACH_NUM, 0)

    @property
    def mimetype(self):
        """Mime-type"""
        return self.get(PR_ATTACH_MIME_TAG_W, u'')

    @mimetype.setter
    def mimetype(self, m):
        self[PR_ATTACH_MIME_TAG_W] = _unicode(m)

    @property
    def filename(self):
        """Filename"""
        return self.get(PR_ATTACH_LONG_FILENAME_W, u'')

    @property
    def embedded(self):
        """Is attachment an embedded message."""
        try:
            return self[PR_ATTACH_METHOD] == ATTACH_EMBEDDED_MSG
        except MAPIErrorNotFound:
            return False

    @property
    def item(self):
        try:
            msg = self.mapiobj.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_DEFERRED_ERRORS | MAPI_MODIFY)
        except MAPIErrorNoAccess:
            # XXX the following may fail for embedded items in certain public stores, while
            # the above does work (opening read-only doesn't work, but read-write works! wut!?)
            msg = self.mapiobj.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_DEFERRED_ERRORS)
        item = _item.Item(mapiobj=msg)
        item.server = self.parent.server
        return item

    @property
    def size(self):
        """Size"""
        # XXX size of the attachment object, so more than just the attachment
        #     data
        # XXX (useful when calculating store size, for example.. sounds
        #     interesting to fix here)
        return self.get(PR_ATTACH_SIZE, 0)

    def __len__(self):
        return self.size

    @property
    def data(self):
        """Binary data"""
        if self._data is None:
            self._data = _utils.stream(self.mapiobj, PR_ATTACH_DATA_BIN)
        return self._data

    # file-like behaviour
    def read(self):
        return self.data

    @property
    def name(self):
        return self.filename

    @property
    def last_modified(self):
        """Last modification time."""
        return self.get(PR_LAST_MODIFICATION_TIME)

    def __unicode__(self):
        return u'Attachment("%s")' % self.name
