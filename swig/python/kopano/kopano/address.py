"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

from .compat import repr as _repr

class Address(object):
    """Address class"""

    def __init__(self, server=None, addrtype=None, name=None, email=None, entryid=None):
        self.server = server
        self.addrtype = addrtype
        self._name = name
        self._email = email
        self.entryid = entryid

    @property
    def name(self):
        """ Full name """

        return self._name or u''

    @property
    def email(self):
        """ Email address """

        if self.addrtype == 'ZARAFA':
            return self.server._resolve_email(entryid=self.entryid)
        else:
            return self._email or ''

    def __unicode__(self):
        return u'Address(%s)' % (self.name or self.email)

    def __repr__(self):
        return _repr(self)
