# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

from .compat import repr as _repr

class Address(object):
    """Address class"""

    def __init__(self, server=None, addrtype=None, name=None, email=None,
                 entryid=None, searchkey=None, props=None):
        self.server = server
        self.addrtype = addrtype
        self._name = name
        self._email = email
        self.entryid = entryid
        self._searchkey = searchkey
        self._props = props

    def props(self):
        """Return all :class:`properties <Property>`."""
        for prop in self._props:
            yield prop

    # XXX prop()

    @property
    def name(self):
        """Full name"""
        return self._name or u''

    @property
    def email(self):
        """Email address"""
        if self.addrtype == 'ZARAFA':
            email = self.server._resolve_email(entryid=self.entryid)
            # cannot resolve email for deleted/non-existent user, so fallback
            # to searchkey
            # XXX make PR_SMTP_ADDRESS always contain email address?
            if (not email and self._searchkey and
                    b':' in self._searchkey and b'@' in self._searchkey):
                email = self._searchkey.split(b':')[1].rstrip(b'\x00').decode('ascii').lower()
        else:
            email = self._email or u''
        return email

    def __unicode__(self):
        return u'Address(%s)' % (self.name or self.email)

    def __repr__(self):
        return _repr(self)
