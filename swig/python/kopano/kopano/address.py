# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import codecs


# TODO use ECParseOneOff? TODO assumes unicode
def _parse_oneoff(content):
    start = sos = 24
    info = []
    while True:
        if content[sos:sos + 2] == b'\x00\x00':
            info.append(content[start:sos].decode('utf-16-le'))
            if len(info) == 3:
                return info
            start = sos + 2
        sos += 2

class Address(object):
    """Address class

    Abstraction for addresses, usually of type SMTP or ZARAFA.
    Most commonly used to resolve full names and/or email addresses.
    """

    def __init__(self, server=None, addrtype=None, name=None, email=None,
                 entryid=None, searchkey=None, props=None, oneoff=None):
        self.server = server

        if oneoff:
            name, addrtype, email = _parse_oneoff(oneoff)

            if addrtype == 'ZARAFA':
                entryid = codecs.decode(server.user(email).userid, 'hex')

        self._entryid = entryid
        self._addrtype = addrtype
        self._name = name
        self._email = email
        self._searchkey = searchkey
        self._props = props

    def props(self):
        """Return associated :class:`properties <Property>`."""
        for prop in self._props:
            yield prop

    # TODO prop()

    @property
    def name(self):
        """Full name"""
        return self._name or ''

    @property
    def email(self):
        """Email address"""
        if self.addrtype == 'ZARAFA':
            email = self.server._resolve_email(entryid=self.entryid)
            # cannot resolve email for deleted/non-existent user, so fallback
            # to searchkey
            # TODO make PR_SMTP_ADDRESS always contain email address?
            if (not email and self._searchkey and
                    b':' in self._searchkey and b'@' in self._searchkey):
                email_bin = self._searchkey.split(b':')[1].rstrip(b'\x00')
                email = email_bin.decode('ascii').lower()
                # Distlists have no email in the searchkey but in PR_SMTP_ADDRESS_W.

            # fallback to the PR_SMTP_ADDRESS_W property.
            if not email and self._props:
                for prop in self._props:
                    if prop.idname == 'PR_SMTP_ADDRESS_W':
                        return prop.value
        else:
            email = self._email or ''
        return email

    @property
    def addrtype(self):
        """Address type (usually SMTP or ZARAFA)"""
        return self._addrtype

    @property
    def entryid(self):
        """User entryid (for addrtype ZARAFA)"""
        return self._entryid

    def __unicode__(self):
        return 'Address(%s)' % (self.name or self.email or '')

    def __repr__(self):
        return self.__unicode__()
