"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2017 - Kopano and its licensors (see LICENSE file)
"""

import sys

from MAPI import (
    KEEP_OPEN_READWRITE, PT_UNICODE
)

from MAPI.Defs import (
    PROP_TYPE
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

    # TODO deprecate in favor of __getitem__
    def value(self, proptag):
        """Return :class:`property <Property>` value for given proptag."""
        return self.prop(proptag).value

    # TODO deprecate in favor of get
    def get_value(self, proptag, default=None):
        """Return :class:`property <Property>` value for given proptag or
        *None* if property does not exist.

        :param proptag: MAPI property tag
        """
        try:
            return self.prop(proptag).value
        except NotFoundError:
            return default

    # TODO deprecate in favor of __setitem__
    def set_value(self, proptag, value):
        """Set :class:`property <Property>` value for given proptag,
        creating the property if it doesn't exist.
        """
        self.prop(proptag, create=True).value = value

    def get(self, proptag, default=None):
        """Return :class:`property <Property>` value for given proptag or
        *None* if property does not exist.

        :param proptag: MAPI property tag
        """
        return self.get_value(proptag, default=default)

    def __getitem__(self, proptag):
        """Return :class:`property <Property>` value for given proptag."""
        return self.get_value(proptag)

    def __setitem__(self, proptag, value):
        """Set :class:`property <Property>` value for given proptag,
        creating the property if it doesn't exist.
        """
        return self.set_value(proptag, value)

    # TODO generalize for any property?
    def _get_fast(self, proptag, default=None, must_exist=False):
        # mapi table cells are limited to 255 characters
        # TODO make the server return PT_ERROR when not-found..!?
        # TODO check other PT types before using them with this!!

        if proptag in self._cache:
            value = self._cache[proptag].value
            if value != 0x8004010f and \
               not (PROP_TYPE(proptag) == PT_UNICODE and len(value) >= 255):
                return value

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
