import os

import icalmapi

from MAPI import MAPI_CREATE, MNID_ID, MNID_STRING, PT_BINARY, PT_MV_UNICODE, PT_BOOLEAN

from MAPI.Defs import CHANGE_PROP_TYPE

from MAPI.Tags import (
    PSETID_Kopano_CalDav, PS_PUBLIC_STRINGS, PR_SUBJECT, PR_SENDER_NAME_W, PR_SENDER_EMAIL_ADDRESS_W,
    PR_START_DATE, PR_END_DATE, PSETID_Appointment
)

from kopano.pidlid import PidLidAppointmentSubType

from MAPI.Struct import MAPINAMEID

from MAPI.Time import FileTime

from conftest import assert_item_count_from_ical, assert_property_value_from_ical, assert_properties_from_ical

dir_path = os.path.dirname(os.path.realpath(__file__))

def test_success_conversion(store, icaltomapi, message):
    ical = open(os.path.join(dir_path, 'ics/success.ics'), 'rb').read()

    # As defined in namedprops.h
    DISPID_APPT_TS_REF = 0x25

    NAMED_PROP_UID = MAPINAMEID(PSETID_Kopano_CalDav, MNID_ID, DISPID_APPT_TS_REF)
    NAMED_PROP_CATEGORY = MAPINAMEID(PS_PUBLIC_STRINGS, MNID_STRING, 'Keywords')
    properties = store.GetIDsFromNames([NAMED_PROP_UID, NAMED_PROP_CATEGORY], MAPI_CREATE)

    EXPECTED_MESSAGES = 1
    UID = CHANGE_PROP_TYPE(properties[0], PT_BINARY)  #0x85100102
    CATEGORY = CHANGE_PROP_TYPE(properties[1], PT_MV_UNICODE) #0x850B101E
    START_DATE_PT_SYSTIME = 135547524000000000
    END_DATE_PT_SYSTIME = 135547920000000000

    prop_dic = {
        UID: [b'mrgreatholidays@rest.com'],
        PR_SUBJECT: [b'A dream holiday in the mountains'],
        CATEGORY: [['Holidays']],
        PR_SENDER_NAME_W: ['John Holidays'],
        PR_SENDER_EMAIL_ADDRESS_W: ['mrgreatholidays@rest.com'],
        PR_START_DATE: [FileTime(START_DATE_PT_SYSTIME)],
        PR_END_DATE: [FileTime(END_DATE_PT_SYSTIME)],
    }

    assert_item_count_from_ical(icaltomapi, ical, EXPECTED_MESSAGES)
    assert_properties_from_ical(icaltomapi, message, prop_dic)

def test_multiple_vevents(icaltomapi, message):
    ical = open(os.path.join(dir_path, 'ics/multiple_vevents.ics'), 'rb').read()
    subjects = [b'What is the meaning of life the universe and everything?', b'42']

    assert_item_count_from_ical(icaltomapi, ical, len(subjects))
    assert_property_value_from_ical(icaltomapi, message, PR_SUBJECT, subjects)

def test_multiple_vcalenders(icaltomapi, message):
    ical = open(os.path.join(dir_path, 'ics/multiple_vcalendar.ics'), 'rb').read()
    subjects = [b'super summary', b'mega summary', b'uber summer', b'it\'s summer!']

    assert_item_count_from_ical(icaltomapi, ical, len(subjects))
    assert_property_value_from_ical(icaltomapi, message, PR_SUBJECT, subjects)

def test_round_trip_all_day_dstart_dend_style(store, icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/allday_dstart_dend_style.ics'), 'rb').read()

    # As defined in namedprops.h
    DISPID_ALL_DAY_EVENT = 0x8215

    EXPECTED_MESSAGES = 1
    START_DATE_PT_SYSTIME = 132587712000000000
    END_DATE_PT_SYSTIME = 132588576000000000

    ALL_DAY_NAMEID = MAPINAMEID(PSETID_Appointment, MNID_ID, DISPID_ALL_DAY_EVENT)
    ALL_DAY_PROP = CHANGE_PROP_TYPE(
        store.GetIDsFromNames([ALL_DAY_NAMEID], MAPI_CREATE)[0],
        PT_BOOLEAN
    )

    prop_dic = {
        PR_START_DATE: [FileTime(START_DATE_PT_SYSTIME)],
        PR_END_DATE: [FileTime(END_DATE_PT_SYSTIME)],
        ALL_DAY_PROP: [True]
    }

    assert_item_count_from_ical(icaltomapi, ical, EXPECTED_MESSAGES)
    assert_properties_from_ical(icaltomapi, message, prop_dic)

    # Reverting back to ical.
    mapitoical.AddMessage(message, '', 0)
    method, converted_ical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    split_ical = converted_ical.split(b'\r\n')
    assert b'PUBLISH' == method
    assert b'DTSTART;VALUE=DATE:20210226' in split_ical
    assert b'DTEND;VALUE=DATE:20210227' in split_ical

def test_round_trip_all_day_dstart_dend_style_not_date_incorrect(store, icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/allday_dstart_dend_style_incorrect.ics'), 'rb').read()

    # As defined in namedprops.h
    DISPID_ALL_DAY_EVENT = 0x8215

    EXPECTED_MESSAGES = 1
    START_DATE_PT_SYSTIME = 132587712000000000
    END_DATE_PT_SYSTIME = 132588576000000000

    ALL_DAY_NAMEID = MAPINAMEID(PSETID_Appointment, MNID_ID, DISPID_ALL_DAY_EVENT)
    ALL_DAY_PROP = CHANGE_PROP_TYPE(
        store.GetIDsFromNames([ALL_DAY_NAMEID], MAPI_CREATE)[0],
        PT_BOOLEAN
    )

    prop_dic = {
        PR_START_DATE: [FileTime(START_DATE_PT_SYSTIME)],
        PR_END_DATE: [FileTime(END_DATE_PT_SYSTIME)],
        ALL_DAY_PROP: [False]
    }

    assert_item_count_from_ical(icaltomapi, ical, EXPECTED_MESSAGES)
    assert_properties_from_ical(icaltomapi, message, prop_dic)

    # Reverting back to ical.
    mapitoical.AddMessage(message, '', 0)
    method, converted_ical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    split_ical = converted_ical.split(b'\r\n')
    assert b'PUBLISH' == method
    assert b'DTSTART:20210226T000000Z' in split_ical
    assert b'DTEND:20210227T000000Z' in split_ical

def test_round_trip_all_day_duration_style(store, icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/allday_duration_style.ics'), 'rb').read()

    # As defined in namedprops.h
    DISPID_ALL_DAY_EVENT = 0x8215

    EXPECTED_MESSAGES = 1
    START_DATE_PT_SYSTIME = 132587712000000000
    END_DATE_PT_SYSTIME = 132589440000000000

    ALL_DAY_NAMEID = MAPINAMEID(PSETID_Appointment, MNID_ID, DISPID_ALL_DAY_EVENT)
    ALL_DAY_PROP = CHANGE_PROP_TYPE(
        store.GetIDsFromNames([ALL_DAY_NAMEID], MAPI_CREATE)[0],
        PT_BOOLEAN
    )

    prop_dic = {
        PR_START_DATE: [FileTime(START_DATE_PT_SYSTIME)],
        PR_END_DATE: [FileTime(END_DATE_PT_SYSTIME)],
        ALL_DAY_PROP: [True]
    }

    assert_item_count_from_ical(icaltomapi, ical, EXPECTED_MESSAGES)
    assert_properties_from_ical(icaltomapi, message, prop_dic)

    # Reverting back to ical.
    mapitoical.AddMessage(message, '', 0)
    method, converted_ical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    split_ical = converted_ical.split(b'\r\n')
    assert b'PUBLISH' == method
    assert b'DTSTART;VALUE=DATE:20210226' in split_ical
    assert b'DTEND;VALUE=DATE:20210228' in split_ical

def test_round_trip_all_day_duration_style_one_week_after(store, icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/allday_duration_style_one_week.ics'), 'rb').read()

    # As defined in namedprops.h
    DISPID_ALL_DAY_EVENT = 0x8215

    EXPECTED_MESSAGES = 1
    START_DATE_PT_SYSTIME = 132587712000000000
    END_DATE_PT_SYSTIME = 132593760000000000

    ALL_DAY_NAMEID = MAPINAMEID(PSETID_Appointment, MNID_ID, DISPID_ALL_DAY_EVENT)
    ALL_DAY_PROP = CHANGE_PROP_TYPE(
        store.GetIDsFromNames([ALL_DAY_NAMEID], MAPI_CREATE)[0],
        PT_BOOLEAN
    )

    prop_dic = {
        PR_START_DATE: [FileTime(START_DATE_PT_SYSTIME)],
        PR_END_DATE: [FileTime(END_DATE_PT_SYSTIME)],
        ALL_DAY_PROP: [True]
    }

    assert_item_count_from_ical(icaltomapi, ical, EXPECTED_MESSAGES)
    assert_properties_from_ical(icaltomapi, message, prop_dic)

    # Reverting back to ical.
    mapitoical.AddMessage(message, '', 0)
    method, converted_ical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    split_ical = converted_ical.split(b'\r\n')
    assert b'PUBLISH' == method
    assert b'DTSTART;VALUE=DATE:20210226' in split_ical
    assert b'DTEND;VALUE=DATE:20210305' in split_ical

def test_round_trip_all_day_duration_style_not_day_incorrect(store, icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/allday_duration_style_incorrect.ics'), 'rb').read()

    # As defined in namedprops.h
    DISPID_ALL_DAY_EVENT = 0x8215

    EXPECTED_MESSAGES = 1
    START_DATE_PT_SYSTIME = 132587712000000000
    END_DATE_PT_SYSTIME = 132587820000000000

    ALL_DAY_NAMEID = MAPINAMEID(PSETID_Appointment, MNID_ID, DISPID_ALL_DAY_EVENT)
    ALL_DAY_PROP = CHANGE_PROP_TYPE(
        store.GetIDsFromNames([ALL_DAY_NAMEID], MAPI_CREATE)[0],
        PT_BOOLEAN
    )

    prop_dic = {
        PR_START_DATE: [FileTime(START_DATE_PT_SYSTIME)],
        PR_END_DATE: [FileTime(END_DATE_PT_SYSTIME)],
        ALL_DAY_PROP: [False]
    }

    assert_item_count_from_ical(icaltomapi, ical, EXPECTED_MESSAGES)
    assert_properties_from_ical(icaltomapi, message, prop_dic)

    # Reverting back to ical.
    mapitoical.AddMessage(message, '', 0)
    method, converted_ical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    split_ical = converted_ical.split(b'\r\n')
    assert b'PUBLISH' == method
    assert b'DTSTART:20210226T000000Z' in split_ical
    assert b'DTEND:20210226T030000Z' in split_ical

def test_round_trip_all_day_microsoft_style(store, icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/allday_ms_style.ics'), 'rb').read()

    # As defined in namedprops.h
    DISPID_ALL_DAY_EVENT = 0x8215

    EXPECTED_MESSAGES = 1
    START_DATE_PT_SYSTIME = 132587712000000000
    END_DATE_PT_SYSTIME = 132588576000000000

    ALL_DAY_NAMEID = MAPINAMEID(PSETID_Appointment, MNID_ID, DISPID_ALL_DAY_EVENT)
    ALL_DAY_PROP = CHANGE_PROP_TYPE(
        store.GetIDsFromNames([ALL_DAY_NAMEID], MAPI_CREATE)[0],
        PT_BOOLEAN
    )

    prop_dic = {
        PR_START_DATE: [FileTime(START_DATE_PT_SYSTIME)],
        PR_END_DATE: [FileTime(END_DATE_PT_SYSTIME)],
        ALL_DAY_PROP: [True]
    }

    assert_item_count_from_ical(icaltomapi, ical, EXPECTED_MESSAGES)
    assert_properties_from_ical(icaltomapi, message, prop_dic)

    # Reverting back to ical. We use the date style so we shouldn't have a time anymore.
    mapitoical.AddMessage(message, '', 0)
    method, converted_ical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    split_ical = converted_ical.split(b'\r\n')
    assert b'PUBLISH' == method
    assert b'DTSTART;VALUE=DATE:20210226' in split_ical
    assert b'DTEND;VALUE=DATE:20210227' in split_ical

def test_microsoft_style_not_all_day_if_not_midnight_incorrect(store, icaltomapi, message):
    ical = open(os.path.join(dir_path, 'ics/allday_ms_style_incorrect.ics'), 'rb').read()

    # As defined in namedprops.h
    DISPID_ALL_DAY_EVENT = 0x8215

    ALL_DAY_NAMEID = MAPINAMEID(PSETID_Appointment, MNID_ID, DISPID_ALL_DAY_EVENT)
    ALL_DAY_PROP = CHANGE_PROP_TYPE(
        store.GetIDsFromNames([ALL_DAY_NAMEID], MAPI_CREATE)[0],
        PT_BOOLEAN
    )

    EXPECTED_MESSAGES = 1
    IS_ALL_DAY = [False]

    assert_item_count_from_ical(icaltomapi, ical, EXPECTED_MESSAGES)
    assert_property_value_from_ical(icaltomapi, message, ALL_DAY_PROP, IS_ALL_DAY)
