"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

import sys

from .compat import repr as _repr
from .errors import NotFoundError

if sys.hexversion >= 0x03000000:
    from . import prop as _prop
else:
    import prop as _prop

class Base(object):

    def prop(self, proptag, create=False, proptype=None):
        """ Return :class:`property <Property> with given property tag

        :param proptag: MAPI property tag
        :param create: create property if it doesn't exist

        """

        return _prop.prop(self, self.mapiobj, proptag, create=create, proptype=proptype)

    def get_prop(self, proptag):
        """ Return :class:`property <Property> with given proptag or *None* if not found

        :param proptag: MAPI property tag
        """

        try:
            return self.prop(proptag)
        except NotFoundError:
            pass

    def create_prop(self, proptag, value, proptype=None):
        """ Create :class:`property <Property> with given proptag

        :param proptag: MAPI property tag
        :param value: property value (or a default value is used)
        """

        return _prop.create_prop(self, self.mapiobj, proptag, value, proptype)

    def props(self, namespace=None):
        """ Return all :class:`properties <Property>` """

        return _prop.props(self.mapiobj, namespace)

    def value(self, proptag):
        """ Return :class:`property <Property> value for given proptag """

        return self.prop(proptag).value

    def get_value(self, proptag):
        """ Return :class:`property <Property> value for given proptag or
        *None* if property does not exist

        :param proptag: MAPI property tag
        """

        try:
            return self.prop(proptag).value
        except NotFoundError:
            pass

    def set_value(self, proptag, value):
        """ Set :class:`property <Property> value for given proptag,
        creating the property if it doesn't exist
        """

        self.prop(proptag, create=True).value = value

    def __repr__(self):
        return _repr(self)
