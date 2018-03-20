"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2017 - Kopano and its licensors (see LICENSE file)
"""

import sys

from MAPI import (
    KEEP_OPEN_READWRITE, PT_UNICODE, PT_ERROR, MAPI_E_NOT_FOUND
)

from MAPI.Defs import (
    PROP_TYPE, PROP_ID
)

from MAPI.Struct import (
    SPropValue
)

from .compat import repr as _repr
from .errors import NotFoundError

if sys.hexversion >= 0x03000000:
    from . import property_ as _prop
else:
    import property_ as _prop

class Properties(object):
    """Property mixin class"""

    def prop(self, proptag, create=False, proptype=None):
        """Return :class:`property <Property>` with given property tag.

        :param proptag: MAPI property tag
        :param create: create property if it doesn't exist
        """
        return _prop.prop(self, self.mapiobj, proptag, create=create,
            proptype=proptype)

    def get_prop(self, proptag):
        """Return :class:`property <Property>` with given proptag or
        *None* if not found.

        :param proptag: MAPI property tag
        """
        try:
            return self.prop(proptag)
        except NotFoundError:
            pass

    def create_prop(self, proptag, value, proptype=None):
        """Create :class:`property <Property>` with given proptag.

        :param proptag: MAPI property tag
        :param value: property value (or a default value is used)
        """
        return _prop.create_prop(self, self.mapiobj, proptag, value, proptype)

    def props(self, namespace=None):
        """Return all :class:`properties <Property>`."""
        return _prop.props(self.mapiobj, namespace)

    # mapi objects/properties are basically key-value stores
    # so the following provides some useful dict-like behaviour

    def get(self, proptag, default=None):
        """Return :class:`property <Property>` value for given proptag or
        *None* if property does not exist.

        :param proptag: MAPI property tag
        """
        try:
            return self.prop(proptag).value
        except NotFoundError:
            return default

    def __getitem__(self, proptag):
        """Return :class:`property <Property>` value for given proptag."""
        return self.prop(proptag).value

    def __setitem__(self, proptag, value):
        """Set :class:`property <Property>` value for given proptag,
        creating the property if it doesn't exist.
        """
        self.prop(proptag, create=True).value = value

    def __delitem__(self, proptag):
        """Delete the :class:`property <Property>` with given proptag."""
        self.delete(self.prop(proptag))

    # the following is faster in case of preloaded/cached table data,
    # because it avoids the backend preloading all item properties with
    # about 20 SQL statements _per item_ (!)

    # TODO generalize for any property?
    def _get_fast(self, proptag, default=None, must_exist=False, capped=False):
        # in cache

        prop = self._cache.get(proptag)
        if prop is not None:
            proptype = PROP_TYPE(prop.proptag)
            value = prop.value

            if proptype == PT_ERROR and value == MAPI_E_NOT_FOUND:
                return default

            # mapi table cells are limited to 255 characters/bytes
            # TODO check other types
            if capped or not (proptype == PT_UNICODE and len(value) >= 255):
                return value

        # fallback to (slow) lookup
        try:
            return self.prop(proptag).value
        except NotFoundError:
            if must_exist:
                raise
            else:
                return default

    def _set_fast(self, proptag, value):
        self._cache.pop(proptag, None)
        self.mapiobj.SetProps([SPropValue(proptag, value)])
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)

    def __repr__(self):
        return _repr(self)
