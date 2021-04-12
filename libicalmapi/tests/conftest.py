import os

import pytest

import icalmapi
import RecurrenceState as RS

from MAPI import MAPI_MODIFY, MAPI_UNICODE
from MAPI.Util import OpenECSession, GetDefaultStore, DELETE_HARD_DELETE
from MAPI.Tags import PR_IPM_APPOINTMENT_ENTRYID


@pytest.fixture
def session():
    user = os.getenv('KOPANO_TEST_USER')
    password = os.getenv('KOPANO_TEST_PASSWORD')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket)


@pytest.fixture
def store(session):
    return GetDefaultStore(session)


@pytest.fixture
def root(store):
    return store.OpenEntry(None, None, MAPI_MODIFY)


@pytest.fixture
def ab(session):
    return session.OpenAddressBook(0, None, MAPI_UNICODE)


@pytest.fixture
def calendar(store, root):
    aeid = root.GetProps([PR_IPM_APPOINTMENT_ENTRYID], 0)
    calendar = store.OpenEntry(aeid[0].Value, None, MAPI_MODIFY)
    yield calendar
    calendar.EmptyFolder(DELETE_HARD_DELETE, None, 0)


@pytest.fixture
def message(calendar):
    return calendar.CreateMessage(None, 0)


@pytest.fixture
def icaltomapi(store, ab):
    return icalmapi.CreateICalToMapi(store, ab, False)


@pytest.fixture
def mapitoical(ab):
    return icalmapi.CreateMapiToICal(ab, 'utf-8')

# This function parses an ical string and asserts the number of expected messages
def assert_item_count_from_ical(icaltomapi, ical, expected_num_messages):
    icaltomapi.ParseICal(ical, 'utf-8', 'UTC', None, 0)
    assert icaltomapi.GetItemCount() == expected_num_messages

# This function retrieves a parsed message from ical and returns a specific property from it
def get_prop_from_ical(icaltomapi, message, prop, index_message):
    icaltomapi.GetItem(index_message, 0, message)
    return message.GetProps([prop], 0)[0].Value

# This function retrieves the glob property from a parsed message from ical
def assert_get_glob_from_ical(icaltomapi, message, ical, expected_num_messages):
    assert_item_count_from_ical(icaltomapi, ical, expected_num_messages)
    return get_prop_from_ical(icaltomapi, message, 0x80160102, 0)

# This function asserts that the messages parsed from ical contain property with a certain value.
# The values should be sent as a list in expected_prop_values
# The function will assume the number of messages is equal to the number of property values in the list.
def assert_property_value_from_ical(icaltomapi, message, prop, expected_prop_values):
    for i in range(len(expected_prop_values)):
        prop_value = get_prop_from_ical(icaltomapi, message, prop, i)
        assert expected_prop_values[i] == prop_value

# This function asserts that the messages parsed from ical contain a set of properties with a specific value.
# The properties should be sent as a dicionary to expected_props_values_dic. The property is the key and the value is a
# list of property values.
# The function will assume the number of messages is equal to the number of property values in each list value in the
# dictionary.
def assert_properties_from_ical(icaltomapi, message, expected_props_values_dic):
    for prop in expected_props_values_dic:
        assert_property_value_from_ical(icaltomapi, message, prop, expected_props_values_dic[prop])

def getrecurrencestate(blob):
    rs = RS.RecurrenceState()
    rs.ParseBlob(blob, 0)
    return rs


def get_item_from_vcf(vcf, message):
    vcm = icalmapi.create_vcftomapi(message)
    vcm.parse_vcf(vcf)
    vcm.get_item(message)
    return message
