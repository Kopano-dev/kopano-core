"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

from MAPI import (
    KEEP_OPEN_READWRITE,
)

from MAPI.Tags import (
    PR_ATTACHMENT_CONTACTPHOTO, PR_GIVEN_NAME_W, PR_MIDDLE_NAME_W,
    PR_SURNAME_W, PR_NICKNAME_W, PR_GENERATION_W, PR_TITLE_W, PR_GENERATION_W,
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
    PidLidEmail1OriginalEntryId, PidLidYomiFirstName, PidLidYomiLastName,
    PidLidYomiCompanyName, PidLidFileUnder, PidLidInstantMessagingAddress,
    PidLidWorkAddressStreet, PidLidWorkAddressCity, PidLidWorkAddressState,
    PidLidWorkAddressPostalCode, PidLidWorkAddressCountry,
)

from .compat import repr as _repr

from .address import Address
from .errors import NotFoundError
from .compat import (
    fake_unicode as _unicode,
)

class PhysicalAddress(object):
    def __init__(self, item, proptags):
        street, city, postal_code, state, country = proptags
        self.street = item.get(street, u'')
        self.city = item.get(city, u'')
        self.postal_code = item.get(postal_code, u'')
        self.state = item.get(state, u'')
        self.country = item.get(country, u'')

    def __repr__(self):
        return _repr(self)

    def __unicode__(self):
        return u'PhysicalAddress()'

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
                return attachment

    def set_photo(self, name, data, mimetype):
        for attachment in self.attachments():
            if attachment.get(PR_ATTACHMENT_CONTACTPHOTO):
                self.delete(attachment)
        attachment = self.create_attachment(name, data)
        attachment[PR_ATTACHMENT_CONTACTPHOTO] = True
        attachment.mimetype = mimetype
        self.mapiobj.SaveChanges(KEEP_OPEN_READWRITE)
        return attachment

    @property
    def initials(self):
        return self.get(PR_INITIALS_W, u'')

    @property
    def first_name(self):
        return self.get(PR_GIVEN_NAME_W, u'')

    @property
    def middle_name(self):
        return self.get(PR_MIDDLE_NAME_W, u'')

    @property
    def last_name(self):
        return self.get(PR_SURNAME_W, u'')

    @property
    def nickname(self):
        return self.get(PR_NICKNAME_W, u'')

    @property
    def title(self):
        return self.get(PR_DISPLAY_NAME_PREFIX_W, u'')

    @property
    def generation(self):
        return self.get(PR_GENERATION_W, u'')

    @property
    def company_name(self):
        return self.get(PR_COMPANY_NAME_W, u'')

    @property
    def children(self):
        try:
            return self[PR_CHILDRENS_NAMES_W]
        except NotFoundError:
            return []

    @property
    def spouse(self):
        return self.get(PR_SPOUSE_NAME_W, u'')

    @property
    def birthday(self):
        return self.get(PR_BIRTHDAY)

    @property
    def yomi_first_name(self):
        return self.get(PidLidYomiFirstName, u'')

    @property
    def yomi_last_name(self):
        return self.get(PidLidYomiLastName, u'')

    @property
    def yomi_company_name(self):
        return self.get(PidLidYomiCompanyName, u'')

    @property
    def file_as(self):
        return self.get(PidLidFileUnder, u'')

    @property
    def job_title(self):
        return self.get(PR_TITLE_W, u'')

    @property
    def department(self):
        return self.get(PR_DEPARTMENT_NAME_W, u'')

    @property
    def office_location(self):
        return self.get(PR_OFFICE_LOCATION_W, u'')

    @property
    def profession(self):
        return self.get(PR_PROFESSION_W, u'')

    @property
    def manager(self):
        return self.get(PR_MANAGER_NAME_W, u'')

    @property
    def assistant(self):
        return self.get(PR_ASSISTANT_W, u'')

    @property
    def business_homepage(self):
        return self.get(PR_BUSINESS_HOME_PAGE_W, u'')

    @property
    def mobile_phone(self):
        return self.get(PR_MOBILE_TELEPHONE_NUMBER_W, u'')

    def _str_list(self, proptags):
        result = []
        for proptag in proptags:
            val = self.get(proptag)
            if val:
                result.append(val)
        return result

    @property
    def home_phones(self):
        return self._str_list(
            [PR_HOME_TELEPHONE_NUMBER_W, PR_HOME2_TELEPHONE_NUMBER_W])

    @property
    def business_phones(self):
        return self._str_list(
            [PR_BUSINESS_TELEPHONE_NUMBER_W, PR_BUSINESS2_TELEPHONE_NUMBER_W])

    @property
    def im_addresses(self):
        return self._str_list([PidLidInstantMessagingAddress])

    @property
    def home_address(self):
        return PhysicalAddress(self, [
            PR_HOME_ADDRESS_STREET_W,
            PR_HOME_ADDRESS_CITY_W,
            PR_HOME_ADDRESS_POSTAL_CODE_W,
            PR_HOME_ADDRESS_STATE_OR_PROVINCE_W,
            PR_HOME_ADDRESS_COUNTRY_W,
        ])

    @property
    def business_address(self):
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
        return PhysicalAddress(self, [
            PR_OTHER_ADDRESS_STREET_W,
            PR_OTHER_ADDRESS_CITY_W,
            PR_OTHER_ADDRESS_POSTAL_CODE_W,
            PR_OTHER_ADDRESS_STATE_OR_PROVINCE_W,
            PR_OTHER_ADDRESS_COUNTRY_W,
        ])
