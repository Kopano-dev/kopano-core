"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

import sys

from MAPI.Struct import MAPIErrorNotFound

from .compat import repr as _repr

if sys.hexversion >= 0x03000000:
    from . import prop as _prop
else:
    import prop as _prop

class Base(object):

    def prop(self, proptag, create=False):
        """ Return :class:`property <Property> with given property tag

        :param proptag: MAPI property tag
        :param create: create property if it doesn't exist

        """

        return _prop.prop(self, self.mapiobj, proptag, create=create)

    def get_prop(self, proptag):
        """ Return :class:`property <Property> with given proptag or *None* if not found

        :param proptag: MAPI property tag
        """

        try:
            return self.prop(proptag)
        except MAPIErrorNotFound:
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

    def __repr__(self):
        return _repr(self)
