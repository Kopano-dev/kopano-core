from MAPI import MAPI_UNICODE
from MAPI.Defs import PROP_TYPE
from MAPI.Tags import (PT_STRING8, PT_UNICODE, TBL_ALL_COLUMNS, )


def assert_check_columns(columns, ulTableFlags, description):
    nWrongType = 0
    for ulPropTag in columns:
        if ((ulTableFlags & MAPI_UNICODE) and PROP_TYPE(ulPropTag) == PT_STRING8):
            nWrongType += 1
        elif ((ulTableFlags & MAPI_UNICODE) == 0 and PROP_TYPE(ulPropTag) == PT_UNICODE):
            nWrongType += 1
    assert nWrongType == 0, description % nWrongType


def assert_profile_columns(adminprof, ulTableFlags, ulColumnFlags, description):
    proftable = adminprof.GetProfileTable(ulTableFlags)
    profcols = proftable.QueryColumns(ulColumnFlags)
    assert_check_columns(profcols, ulTableFlags, description)


def test_profilecolumns_defaultascii(adminprof):
    assert_profile_columns(adminprof, 0, 0, '%d unicode strings found in ascii default columns')

def test_profilecolumns_defaultunicode(adminprof):
    assert_profile_columns(adminprof, MAPI_UNICODE, 0, '%d ascii strings found in unicode default columns')

def test_profilecolumns_allascii(adminprof):
    assert_profile_columns(adminprof, 0, TBL_ALL_COLUMNS, '%d unicode strings found in ascii all columns')

def test_profilecolumns_allunicode(adminprof):
    assert_profile_columns(adminprof, MAPI_UNICODE, TBL_ALL_COLUMNS, '%d ascii strings found in unicode all columns')


def assert_provider_columns(adminservice, ulTableFlags, ulColumnFlags, description):
    providertable = adminservice.GetProviderTable(ulTableFlags)
    providercols = providertable.QueryColumns(ulColumnFlags)
    assert_check_columns(providercols, ulTableFlags, description)


def testProviderColumnsDefaultAscii(adminservice):
    assert_provider_columns(adminservice, 0, 0, '%d unicode strings found in ascii default columns')

def testProviderColumnsDefaultUnicode(adminservice):
    assert_provider_columns(adminservice, MAPI_UNICODE, 0, '%d ascii strings found in unicode default columns')

def testProviderColumnsAllAscii(adminservice):
    assert_provider_columns(adminservice, 0, TBL_ALL_COLUMNS, '%d unicode strings found in ascii all columns')

def testProviderColumnsAllUnicode(adminservice):
    assert_provider_columns(adminservice, MAPI_UNICODE, TBL_ALL_COLUMNS, '%d ascii strings found in unicode all columns')


def asssert_service_columns(adminservice, ulTableFlags, ulColumnFlags, description):
    msgservicetable = adminservice.GetMsgServiceTable(ulTableFlags)
    msgservicecols = msgservicetable.QueryColumns(ulColumnFlags)
    assert_check_columns(msgservicecols, ulTableFlags, description)

def testMsgServiceColumnsDefaultAscii(adminservice):
    asssert_service_columns(adminservice, 0, 0, '%d unicode strings found in ascii default columns')

def testMsgServiceColumnsDefaultUnicode(adminservice):
    asssert_service_columns(adminservice, MAPI_UNICODE, 0, '%d ascii strings found in unicode default columns')

def testMsgServiceColumnsAllAscii(adminservice):
    asssert_service_columns(adminservice, 0, TBL_ALL_COLUMNS, '%d unicode strings found in ascii all columns')

def testMsgServiceColumnsAllUnicode(adminservice):
    asssert_service_columns(adminservice, MAPI_UNICODE, TBL_ALL_COLUMNS, '%d ascii strings found in unicode all columns')
