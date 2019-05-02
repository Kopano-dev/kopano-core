# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Part of the high-level python bindings for Kopano

Copyright 2018 - Kopano and its licensors (see LICENSE file for details)
"""

import io
import mimetypes


WITH_PIL = False
try:
    from PIL import Image
    WITH_PIL = True
except ImportError: # pragma: no cover
    pass

from .compat import repr as _repr

class Picture(object):
    """Picture class"""

    def __init__(self, data, name=u'', mimetype=u''):
        self.data = data
        self.name = name
        self._mimetype = mimetype
        self.__img = None

    @property
    def _img(self):
        if self.__img is None and WITH_PIL:
            self.__img = Image.open(io.BytesIO(self.data))
        return self.__img

    @property
    def mimetype(self):
        if not self._mimetype:
            ext = self._img.format
            if ext:
                self._mimetype = mimetypes.types_map.get('.'+ext.lower(), u'')
        return self._mimetype

    @property
    def width(self):
        return self._img.width

    @property
    def height(self):
        return self._img.height

    @property
    def size(self):
        return (self.width, self.height)

    def scale(self, size):
        if not WITH_PIL:
            raise NotImplementedError('PIL is not available')
        img = Image.open(io.BytesIO(self.data))
        img.thumbnail(size)
        b = io.BytesIO()
        img.save(b, format=img.format)
        data = b.getvalue()
        return Picture(data, self.name, self.mimetype)

    def __unicode__(self):
        return u'Picture(%s)' % (self.name or u'')

    def __repr__(self):
        return _repr(self)
