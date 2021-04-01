import os

from conftest import get_item_from_vcf

from MAPI.Tags import (PR_COMPANY_NAME_W, PR_DEPARTMENT_NAME_W, PR_BIRTHDAY, PR_WEDDING_ANNIVERSARY)


dir_path = os.path.dirname(os.path.realpath(__file__))


def test_company(message):
    vcf = open(os.path.join(dir_path, 'vcf/company.vcf'), 'rb').read()
    message = get_item_from_vcf(vcf, message)

    props = message.GetProps([PR_COMPANY_NAME_W], 0)
    assert props[0].Value == 'company'


def test_company_org(message):
    vcf = open(os.path.join(dir_path, 'vcf/company_org.vcf'), 'rb').read()
    message = get_item_from_vcf(vcf, message)

    props = message.GetProps([PR_COMPANY_NAME_W, PR_DEPARTMENT_NAME_W], 0)
    assert props[0].Value == 'company'
    assert props[1].Value == 'department'


def test_datefields(message):
    vcf = open(os.path.join(dir_path, 'vcf/datefields.vcf'), 'rb').read()
    message = get_item_from_vcf(vcf, message)

    props = message.GetProps([PR_BIRTHDAY, PR_WEDDING_ANNIVERSARY], 0)
    assert props[0].Value.unixtime == 1616713200.0
    assert props[1].Value.unixtime == 1616713200.0


def test_datefield_formats(message):
    vcf = open(os.path.join(dir_path, 'vcf/datefields_formats.vcf'), 'rb').read()
    message = get_item_from_vcf(vcf, message)

    props = message.GetProps([PR_BIRTHDAY, PR_WEDDING_ANNIVERSARY], 0)
    assert props[0].Value.unixtime == 1616630400.0
    assert props[1].Value.unixtime == 1616630400.0
