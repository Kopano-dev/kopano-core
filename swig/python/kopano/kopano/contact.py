# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import sys

from MAPI.Tags import (
    PR_ATTACHMENT_CONTACTPHOTO, PR_GIVEN_NAME_W, PR_MIDDLE_NAME_W,
    PR_SURNAME_W, PR_NICKNAME_W, PR_TITLE_W, PR_GENERATION_W,
    PR_COMPANY_NAME_W, PR_MOBILE_TELEPHONE_NUMBER_W, PR_CHILDRENS_NAMES_W,
    PR_BIRTHDAY, PR_SPOUSE_NAME_W, PR_INITIALS_W, PR_DISPLAY_NAME_PREFIX_W,
    PR_DEPARTMENT_NAME_W, PR_OFFICE_LOCATION_W, PR_PROFESSION_W,
    PR_MANAGER_NAME_W, PR_ASSISTANT_W, PR_BUSINESS_HOME_PAGE_W,
    PR_HOME_TELEPHONE_NUMBER_W, PR_HOME2_TELEPHONE_NUMBER_W,
    PR_BUSINESS_TELEPHONE_NUMBER_W, PR_BUSINESS2_TELEPHONE_NUMBER_W,
    PR_HOME_ADDRESS_STREET_W, PR_HOME_ADDRESS_CITY_W,
    PR_HOME_ADDRESS_POSTAL_CODE_W, PR_HOME_ADDRESS_STATE_OR_PROVINCE_W,
    PR_HOME_ADDRESS_COUNTRY_W, PR_OTHER_ADDRESS_STREET_W,
    PR_OTHER_ADDRESS_CITY_W, PR_OTHER_ADDRESS_POSTAL_CODE_W,
    PR_OTHER_ADDRESS_STATE_OR_PROVINCE_W, PR_OTHER_ADDRESS_COUNTRY_W,
)

from .pidlid import (
    PidLidEmail1AddressType, PidLidEmail1DisplayName, PidLidEmail1EmailAddress,
    PidLidEmail1OriginalEntryId, PidLidEmail2AddressType,
    PidLidEmail2DisplayName,
    PidLidEmail2EmailAddress, PidLidEmail2OriginalEntryId, PidLidYomiFirstName,
    PidLidEmail3AddressType, PidLidEmail3DisplayName, PidLidEmail3EmailAddress,
    PidLidEmail3OriginalEntryId, PidLidYomiLastName, PidLidYomiCompanyName,
    PidLidFileUnder, PidLidInstantMessagingAddress, PidLidWorkAddressStreet,
    PidLidWorkAddressCity, PidLidWorkAddressState, PidLidWorkAddressPostalCode,
    PidLidWorkAddressCountry,
)

from .compat import repr as _repr

from .address import Address
from .errors import NotFoundError
from .compat import (
    fake_unicode as _unicode,
)
from .picture import Picture

try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__ + '.utils']

class PhysicalAddress(object):
    """PhysicalAddress class

    Abstraction for physical addresses.
    """
    def __init__(self, item, proptags):
        street, city, postal_code, state, country = proptags
        #: Street
        self.street = item.get(street, '')
        #: City
        self.city = item.get(city, '')
        #: Postal code
        self.postal_code = item.get(postal_code, '')
        #: State
        self.state = item.get(state, '')
        #: Country
        self.country = item.get(country, '')

    def __repr__(self):
        return _repr(self)

    def __unicode__(self):
        return 'PhysicalAddress()'

class Contact(object):
    """Contact (mixin) class

    Contact-specific functionality, mixed into the :class:`items <Item>`
    class. Only used to separate out this functionality.
    """

    @property
    def email(self):
        """Primary email address."""
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
        """Primary :class:`address <Address>`."""
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
        """Return all :class:`addresses <Address>`."""
        yield self.address1

    @property
    def email2(self):
        """Secondary email address."""
        if self.address1:
            return self.address2.email

    @email2.setter
    def email2(self, addr):  # TODO should be email address?
        self.address2 = addr

    @property
    def address2(self):
        """Secondary :class:`address <Address>`."""
        return Address(
            self.server,
            self.get(PidLidEmail2AddressType),
            self.get(PidLidEmail2DisplayName),
            self.get(PidLidEmail2EmailAddress),
            self.get(PidLidEmail2OriginalEntryId),
        )

    @address2.setter
    def address2(self, addr):
        pr_addrtype, pr_dispname, pr_email, pr_entryid = \
            self._addr_props(addr)

        self[PidLidEmail2AddressType] = _unicode(pr_addrtype)
        self[PidLidEmail2DisplayName] = pr_dispname
        self[PidLidEmail2EmailAddress] = pr_email
        self[PidLidEmail2OriginalEntryId] = pr_entryid

    @property
    def email3(self):
        """Tertiary email address."""
        if self.address3:
            return self.address3.email

    @email3.setter
    def email3(self, addr):  # TODO should be email address?
        self.address3 = addr

    @property
    def address3(self):
        """Tertiary :class:`address <Address>`."""
        return Address(
            self.server,
            self.get(PidLidEmail3AddressType),
            self.get(PidLidEmail3DisplayName),
            self.get(PidLidEmail3EmailAddress),
            self.get(PidLidEmail3OriginalEntryId),
        )

    @address3.setter
    def address3(self, addr):
        pr_addrtype, pr_dispname, pr_email, pr_entryid = \
            self._addr_props(addr)

        self[PidLidEmail3AddressType] = _unicode(pr_addrtype)
        self[PidLidEmail3DisplayName] = pr_dispname
        self[PidLidEmail3EmailAddress] = pr_email
        self[PidLidEmail3OriginalEntryId] = pr_entryid

    # TODO uniformize with user.photo? class Picture?
    @property
    def photo(self):
        """Contact :class:`photo <Picture>`"""
        for attachment in self.attachments():
            if attachment.get(PR_ATTACHMENT_CONTACTPHOTO):
                return Picture(data=attachment.data, name=attachment.name,
                    mimetype=attachment.mimetype)

    def set_photo(self, name, data, mimetype):
        """Set contact :class:`photo <Picture>`

        :param name: file name
        :param data: file binary data
        :param mimetype: file MIME type
        """

        for attachment in self.attachments():
            if attachment.get(PR_ATTACHMENT_CONTACTPHOTO):
                self.delete(attachment)
        attachment = self.create_attachment(name, data)
        attachment[PR_ATTACHMENT_CONTACTPHOTO] = True
        attachment.mimetype = mimetype
        _utils._save(self.mapiobj)
        return attachment

    @property
    def initials(self):
        """Initials."""
        return self.get(PR_INITIALS_W, '')

    @property
    def first_name(self):
        """First name."""
        return self.get(PR_GIVEN_NAME_W, '')

    @property
    def middle_name(self):
        """Middle name."""
        return self.get(PR_MIDDLE_NAME_W, '')

    @property
    def last_name(self):
        """Last name."""
        return self.get(PR_SURNAME_W, '')

    @property
    def nickname(self):
        """Nickname."""
        return self.get(PR_NICKNAME_W, '')

    @property
    def title(self):
        """Title."""
        return self.get(PR_DISPLAY_NAME_PREFIX_W, '')

    @property
    def generation(self):
        """Generation."""
        return self.get(PR_GENERATION_W, '')

    @property
    def company_name(self):
        """Company name."""
        return self.get(PR_COMPANY_NAME_W, '')

    @property
    def children(self):
        """List of children names."""
        try:
            return self[PR_CHILDRENS_NAMES_W]
        except NotFoundError:
            return []

    @property
    def spouse(self):
        """Spouse."""
        return self.get(PR_SPOUSE_NAME_W, '')

    @property
    def birthday(self):
        """Birthday."""
        return self.get(PR_BIRTHDAY)

    @property
    def yomi_first_name(self):
        """Yomi (phonetic) first name."""
        return self.get(PidLidYomiFirstName, '')

    @property
    def yomi_last_name(self):
        """Yomi (phonetic) last name."""
        return self.get(PidLidYomiLastName, '')

    @property
    def yomi_company_name(self):
        """Yomi (phonetic) company name."""
        return self.get(PidLidYomiCompanyName, '')

    @property
    def file_as(self):
        """File as."""
        return self.get(PidLidFileUnder, '')

    @property
    def job_title(self):
        """Job title."""
        return self.get(PR_TITLE_W, '')

    @property
    def department(self):
        """Department."""
        return self.get(PR_DEPARTMENT_NAME_W, '')

    @property
    def office_location(self):
        """Office location."""
        return self.get(PR_OFFICE_LOCATION_W, '')

    @property
    def profession(self):
        """Profession."""
        return self.get(PR_PROFESSION_W, '')

    @property
    def manager(self):
        """Manager."""
        return self.get(PR_MANAGER_NAME_W, '')

    @property
    def assistant(self):
        """Assistant."""
        return self.get(PR_ASSISTANT_W, '')

    @property
    def business_homepage(self):
        """Business homepage."""
        return self.get(PR_BUSINESS_HOME_PAGE_W, '')

    @property
    def mobile_phone(self):
        """Mobile phone."""
        return self.get(PR_MOBILE_TELEPHONE_NUMBER_W, '')

    def _str_list(self, proptags):
        result = []
        for proptag in proptags:
            val = self.get(proptag)
            if val:
                result.append(val)
        return result

    @property
    def home_phones(self):
        """List of home phone numbers."""
        return self._str_list(
            [PR_HOME_TELEPHONE_NUMBER_W, PR_HOME2_TELEPHONE_NUMBER_W])

    @property
    def business_phones(self):
        """List of business phone numbers."""
        return self._str_list(
            [PR_BUSINESS_TELEPHONE_NUMBER_W, PR_BUSINESS2_TELEPHONE_NUMBER_W])

    @property
    def im_addresses(self):
        """List of instant messaging addresses."""
        return self._str_list([PidLidInstantMessagingAddress])

    @property
    def home_address(self):
        """Home :class:`address <PhysicalAddress>`."""
        return PhysicalAddress(self, [
            PR_HOME_ADDRESS_STREET_W,
            PR_HOME_ADDRESS_CITY_W,
            PR_HOME_ADDRESS_POSTAL_CODE_W,
            PR_HOME_ADDRESS_STATE_OR_PROVINCE_W,
            PR_HOME_ADDRESS_COUNTRY_W,
        ])

    @property
    def business_address(self):
        """Business :class:`address <PhysicalAddress>`."""
        # TODO why not PR_BUSINESS_ADDRESS_*..?
        return PhysicalAddress(self, [
            PidLidWorkAddressStreet,
            PidLidWorkAddressCity,
            PidLidWorkAddressPostalCode,
            PidLidWorkAddressState,
            PidLidWorkAddressCountry,
        ])

    @property
    def other_address(self):
        """Other :class:`address <PhysicalAddress>`."""
        return PhysicalAddress(self, [
            PR_OTHER_ADDRESS_STREET_W,
            PR_OTHER_ADDRESS_CITY_W,
            PR_OTHER_ADDRESS_POSTAL_CODE_W,
            PR_OTHER_ADDRESS_STATE_OR_PROVINCE_W,
            PR_OTHER_ADDRESS_COUNTRY_W,
        ])
