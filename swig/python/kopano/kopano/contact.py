"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

try:
    from StringIO import StringIO
except ImportError:
    from io import StringIO

from MAPI import (
    KEEP_OPEN_READWRITE,
)

from MAPI.Tags import (
    PR_ATTACHMENT_CONTACTPHOTO,
)

from .address import Address

PidLidEmail1AddressType = 'PT_UNICODE:PSETID_Address:0x8082'
PidLidEmail1DisplayName = 'PT_UNICODE:PSETID_Address:0x8080'
PidLidEmail1EmailAddress = 'PT_UNICODE:PSETID_Address:0x8083'
PidLidEmail1OriginalEntryId = 'PT_BINARY:PSETID_Address:0x8085'
#PidLidEmail1OriginalDisplayName = 'PT_UNICODE:PSETID_Address:0x8084'

class Contact(object):
    """Contact mixin class"""

    @property
    def email(self):
        return self.email1

    @email.setter
    def email(self, addr):
        self.email1 = addr

    @property
    def email1(self):
        if self.address1:
            return self.address1.email

    @email1.setter
    def email1(self, addr):
        self.address1 = addr

    @property
    def address1(self):
        return Address(
            self.server,
            self.get_value(PidLidEmail1AddressType),
            self.get_value(PidLidEmail1DisplayName),
            self.get_value(PidLidEmail1EmailAddress),
            self.get_value(PidLidEmail1OriginalEntryId),
        )

    @address1.setter
    def address1(self, addr):
        pr_addrtype, pr_dispname, pr_email, pr_entryid = \
            self._addr_props(addr)

        self.set_value(PidLidEmail1AddressType, _unicode(pr_addrtype))
        self.set_value(PidLidEmail1DisplayName, pr_dispname)
        self.set_value(PidLidEmail1EmailAddress, pr_email)
        self.set_value(PidLidEmail1OriginalEntryId, pr_entryid)

    @property
    def photo(self):
        for attachment in self.attachments():
            if attachment.get_value(PR_ATTACHMENT_CONTACTPHOTO):
                s = StringIO(attachment.data)
                s.name = attachment.name
                return s

    @photo.setter
    def photo(self, f):
        name, data = f.name, f.read()
        for attachment in self.attachments():
            if attachment.get_value(PR_ATTACHMENT_CONTACTPHOTO):
                self.delete(attachment)
        attachment = self.create_attachment(name, data)
        attachment.set_value(PR_ATTACHMENT_CONTACTPHOTO, True)
        # XXX shouldn't be needed?
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
