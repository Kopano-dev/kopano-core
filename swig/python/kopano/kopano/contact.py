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
    PR_ATTACHMENT_CONTACTPHOTO, PR_GIVEN_NAME_W, PR_MIDDLE_NAME_W,
    PR_SURNAME_W, PR_NICKNAME_W, PR_GENERATION_W, PR_TITLE_W, PR_GENERATION_W,
    PR_COMPANY_NAME_W, PR_MOBILE_TELEPHONE_NUMBER_W,
)

from .pidlid import (
    PidLidEmail1AddressType, PidLidEmail1DisplayName, PidLidEmail1EmailAddress,
    PidLidEmail1OriginalEntryId,
)

from .address import Address
from .compat import (
    fake_unicode as _unicode,
)

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

    # TODO name1 (setter) etc..?

    @email1.setter
    def email1(self, addr): # TODO should be email address?
        self.address1 = addr

    @property
    def address1(self):
        return Address(
            self.server,
            self.get(PidLidEmail1AddressType),
            self.get(PidLidEmail1DisplayName),
            self.get(PidLidEmail1EmailAddress),
            self.get(PidLidEmail1OriginalEntryId),
        )

    @address1.setter
    def address1(self, addr):
        pr_addrtype, pr_dispname, pr_email, pr_entryid = \
            self._addr_props(addr)

        self[PidLidEmail1AddressType] = _unicode(pr_addrtype)
        self[PidLidEmail1DisplayName] = pr_dispname
        self[PidLidEmail1EmailAddress] = pr_email
        self[PidLidEmail1OriginalEntryId] = pr_entryid

    def addresses(self): # TODO multiple
        yield self.address1

    @property
    def photo(self):
        for attachment in self.attachments():
            if attachment.get(PR_ATTACHMENT_CONTACTPHOTO):
                s = StringIO(attachment.data)
                s.name = attachment.name
                return s

    @photo.setter
    def photo(self, f):
        name, data = f.name, f.read()
        for attachment in self.attachments():
            if attachment.get(PR_ATTACHMENT_CONTACTPHOTO):
                self.delete(attachment)
        attachment = self.create_attachment(name, data)
        attachment[PR_ATTACHMENT_CONTACTPHOTO] = True
        # XXX shouldn't be needed?
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)


    @property
    def given_name(self):
        return self.get(PR_GIVEN_NAME_W)

    @property
    def middle_name(self):
        return self.get(PR_MIDDLE_NAME_W)

    @property
    def surname(self):
        return self.get(PR_SURNAME_W)

    @property
    def nickname(self):
        return self.get(PR_NICKNAME_W)

    @property
    def title(self):
        # TODO webapp uses PR_DISPLAY_NAME_PREFIX_W..?
        return self.get(PR_TITLE_W)

    @property
    def generation(self):
        return self.get(PR_GENERATION_W)

    @property
    def company_name(self):
        return self.get(PR_COMPANY_NAME_W)

    @property
    def mobile_phone(self):
        return self.get(PR_MOBILE_TELEPHONE_NUMBER_W)
