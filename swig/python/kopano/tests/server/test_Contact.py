import pytest

from conftest import ATTACHMENT_PATH, KOPANO_PICTURE_NAME

EMAIL = 'foo@bar.com'


@pytest.mark.parametrize('field', ['email', 'email1', 'email2', 'email3'])
def test_email(contact, field):
    assert not getattr(contact, field)
    setattr(contact, field, EMAIL)
    assert getattr(contact, field) == EMAIL


@pytest.mark.parametrize('field', ['address1', 'address2', 'address3'])
def test_address(user, contact, field):
    setattr(contact, field, user)
    address = getattr(contact, field)

    assert address.name == user.name
    assert address.email == user.email


def test_addresses(contact):
    assert list(contact.addresses())


def test_photo(contact):
    assert not contact.photo

    data = open(ATTACHMENT_PATH.format(KOPANO_PICTURE_NAME), 'rb').read()
    print(KOPANO_PICTURE_NAME)
    contact.set_photo(KOPANO_PICTURE_NAME, data, 'image/png')

    photo = contact.photo
    assert photo
    assert photo.name == KOPANO_PICTURE_NAME
    assert photo.mimetype == 'image/png'
    assert photo.data == data

# VCF

def test_initials(vcfcontact):
    assert not vcfcontact.initials

def test_firstname(vcfcontact):
    assert vcfcontact.first_name == 'pieter'

def test_middlename(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.middle_name

def test_lastname(vcfcontact):
    assert vcfcontact.last_name == 'post'

def test_nickname(vcfcontact):
    assert vcfcontact.nickname == 'harry'

def test_title(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.title

def test_generation(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.generation

def test_companyname(vcfcontact):
    assert vcfcontact.company_name == 'Bubba Gump Shrimp Co.'

def test_children(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.children

def test_spouse(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.spouse

def test_birthday(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.birthday

def test_yomifirstname(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.yomi_first_name

def test_yomilastname(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.yomi_last_name

def test_yomicompanyname(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.yomi_company_name

def test_fileas(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.file_as


def test_jobtitle(vcfcontact):
    assert vcfcontact.job_title == 'Shrimp Man'


def test_department(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.department


def test_officelocation(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.office_location


def test_profession(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.profession


def test_manager(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.manager


def test_assistant(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.assistant


def test_bussinesshomepage(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.business_homepage


def test_mobilephone(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.mobile_phone


def test_homephone(vcfcontact):
    assert vcfcontact.home_phones == ['(404) 555-1212']


def test_businessphone(vcfcontact):  # TODO how to get from VCF?
    assert vcfcontact.business_phones == ['(111) 555-1212']


def test_imaddress(vcfcontact):  # TODO how to get from VCF?
    assert not vcfcontact.im_addresses


def test_homeaddress(vcfcontact):  # TODO non-empty
    assert vcfcontact.home_address.street


def test_businessaddress(vcfcontact):  # TODO non-empty
    assert vcfcontact.business_address.street


def test_otheraddress(vcfcontact):  # TODO non-empty
    assert not vcfcontact.other_address.street
