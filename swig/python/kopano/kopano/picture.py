# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Part of the high-level python bindings for Kopano

Copyright 2018 - 2019 Kopano and its licensors (see LICENSE file)
"""

import io
import logging
import mimetypes


WITH_PIL = False
try:
    from PIL import Image, PngImagePlugin
    WITH_PIL = True
except ImportError: # pragma: no cover
    pass

from .compat import repr as _repr


if WITH_PIL:
    # Get rid of "STREAM" debug messages which might show up for PNG pictures,
    # when the global logger is at DEBUG level.
    logging.getLogger(PngImagePlugin.__name__).setLevel(logging.INFO)

class Picture(object):
    """Picture class

    Typical pictures are :class:`contact <Contact>` and :class:`user <User>`
    profile photos.
    """

    def __init__(self, data, name='', mimetype=''):
        #: Picture name.
        self.name = name
        #: Picture binary data.
        self.data = data
        self._mimetype = mimetype
        self.__img = None

    @property
    def _img(self):
        if self.__img is None and WITH_PIL:
            self.__img = Image.open(io.BytesIO(self.data))
        return self.__img

    @property
    def mimetype(self):
        """Picture MIME type."""
        if not self._mimetype and self._img is not None:
            ext = self._img.format
            if ext:
                self._mimetype = mimetypes.types_map.get('.'+ext.lower(), '')
        return self._mimetype

    @property
    def width(self):
        """Picture pixel width."""
        return self._img.width

    @property
    def height(self):
        """Picture pixel height."""
        return self._img.height

    @property
    def size(self):
        """Picture pixel (width, height) tuple."""
        return (self.width, self.height)

    def scale(self, size):
        """Return new picture instance, scaled to given size.

        :param size: (width, height) tuple
        """
        if not WITH_PIL:
            raise NotImplementedError('PIL is not available')
        img = Image.open(io.BytesIO(self.data))
        img.thumbnail(size)
        b = io.BytesIO()
        img.save(b, format=img.format)
        data = b.getvalue()
        return Picture(data, self.name, self.mimetype)

    def __unicode__(self):
        return 'Picture(%s)' % (self.name or '')

    def __repr__(self):
        return _repr(self)
