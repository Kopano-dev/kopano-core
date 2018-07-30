"""
Part of the high-level python bindings for Kopano.

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - Kopano and its licensors (see LICENSE file)
"""

import re
import struct

from MAPI.Defs import DEFINE_OLEGUID, DEFINE_GUID
from MAPI.Struct import MAPINAMEID
from MAPI import Tags
from MAPI.Tags import (
    PS_PUBLIC_STRINGS
)
from MAPI import MNID_STRING, MAPI_DISTLIST, MNID_ID

try:
    REV_TYPE
except NameError:
    REV_TYPE = {}

    import _MAPICore
    for K, V in _MAPICore.__dict__.items():
        if K.startswith('PT_'):
            REV_TYPE[V] = K

try:
    REV_TAG
except NameError:
    REV_TAG = {}
    for K in sorted(Tags.__dict__, reverse=True):
        if K.startswith('PR_'):
            REV_TAG[Tags.__dict__[K]] = K

PSETID_Appointment = DEFINE_OLEGUID(0x00062002, 0, 0)
PSETID_Task = DEFINE_OLEGUID(0x00062003, 0, 0)
PSETID_Address = DEFINE_OLEGUID(0x00062004, 0, 0)
PSETID_Common = DEFINE_OLEGUID(0x00062008, 0, 0)
PSETID_Log = DEFINE_OLEGUID(0x0006200A, 0, 0)
PSETID_Note = DEFINE_OLEGUID(0x0006200E, 0, 0)
PSETID_Sharing = DEFINE_OLEGUID(0x00062040, 0, 0)
PSETID_PostRss = DEFINE_OLEGUID(0x00062041, 0, 0)
PS_INTERNET_HEADERS = DEFINE_OLEGUID(0x00020386, 0, 0)

PSETID_Archive = DEFINE_GUID(
    0x72e98ebc, 0x57d2, 0x4ab5, 0xb0, 0xaa, 0xd5, 0x0a, 0x7b, 0x53, 0x1c, 0xb9
)
PSETID_Meeting = DEFINE_GUID(
    0x6ED8DA90, 0x450B, 0x101B, 0x98, 0xDA, 0x00, 0xAA, 0x00, 0x3F, 0x13, 0x05
)
PSETID_Kopano_CalDav = DEFINE_GUID(
    0x77536087, 0xcb81, 0x4dc9, 0x99, 0x58, 0xea, 0x4c, 0x51, 0xbe, 0x34, 0x86
)
PSETID_AirSync = DEFINE_GUID(
    0x71035549, 0x0739, 0x4DCB, 0x91, 0x63, 0x00, 0xF0, 0x58, 0x0D, 0xBB, 0xDF
)
PSETID_UnifiedMessaging = DEFINE_GUID(
    0x4442858E, 0xA9E3, 0x4E80, 0xB9, 0x00, 0x31, 0x7A, 0x21, 0x0C, 0xC1, 0x5B
)
PSETID_CONTACT_FOLDER_RECIPIENT = DEFINE_GUID(
    0x0AAA42FE, 0xC718, 0x101A, 0xE8, 0x85, 0x0B, 0x65, 0x1C, 0x24, 0x00, 0x00
)
PSETID_ZMT = DEFINE_GUID(
    0x8acdbf85, 0x4738, 0x4dc4, 0x94, 0xa9, 0xd4, 0x89, 0xa8, 0x3e, 0x5c, 0x41
)
PSETID_CalendarAssistant = DEFINE_GUID(
    0x11000E07, 0xB51B, 0x40D6, 0xAF, 0x21, 0xCA, 0xA8, 0x5E, 0xDA, 0xB1, 0xD0
)
PS_EC_IMAP = DEFINE_GUID(
    0x00f5f108, 0x8e3f, 0x46c7, 0xaf, 0x72, 0x5e, 0x20, 0x1c, 0x23, 0x49, 0xe7
)

PSETID_KC = DEFINE_GUID(
    0x63aed8c8, 0x4049, 0x4b75, 0xbc, 0x88, 0x96, 0xdf, 0x9d, 0x72, 0x3f, 0x2f
)

NAMED_PROPS_INTERNET_HEADERS = [
    MAPINAMEID(PS_INTERNET_HEADERS, MNID_STRING, u'x-original-to'),
]

NAMED_PROPS_ARCHIVER = [
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'store-entryids'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'item-entryids'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'stubbed'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'ref-store-entryid'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'ref-item-entryid'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'ref-prev-entryid'),
    MAPINAMEID(PSETID_Archive, MNID_STRING, u'flags')
]

NAMED_PROPS_KC = [
    MAPINAMEID(PSETID_KC, MNID_ID, 0x0001)
]

NAMED_PROP_CATEGORY = MAPINAMEID(PS_PUBLIC_STRINGS, MNID_STRING, u'Keywords')

GUID_NAMESPACE = {
    PSETID_Archive: 'archive',
    PSETID_Common: 'common',
    PSETID_Appointment: 'appointment',
    PSETID_Task: 'task',
    PSETID_Address: 'address',
    PSETID_Log: 'log',
    PS_INTERNET_HEADERS: 'internet_headers',
    PSETID_Meeting: 'meeting',
    PS_EC_IMAP: 'imap',
    PSETID_Kopano_CalDav: 'kopano_caldav',
    PSETID_AirSync: 'airsync',
    PSETID_Sharing: 'sharing',
    PSETID_PostRss: 'postrss',
    PSETID_UnifiedMessaging: 'unifiedmessaging',
    PSETID_CONTACT_FOLDER_RECIPIENT: 'contact_folder_recipient',
    PSETID_ZMT: 'zmt',
    PSETID_CalendarAssistant: 'calendarassistant',
    PSETID_KC: 'kc',
    PS_PUBLIC_STRINGS: 'public',
}
NAMESPACE_GUID = dict((b, a) for (a, b) in GUID_NAMESPACE.items())

STR_GUID = {
    'PSETID_Common': PSETID_Common,
    'PSETID_Appointment': PSETID_Appointment,
    'PSETID_Meeting': PSETID_Meeting,
    'PSETID_Address': PSETID_Address,
    'PSETID_Archive': PSETID_Archive,
    'PSETID_Task': PSETID_Task,
    'PSETID_Log': PSETID_Log,
    'PS_INTERNET_HEADERS': PS_INTERNET_HEADERS,
    'PS_EC_IMAP': PS_EC_IMAP,
    'PSETID_Kopano_CalDav': PSETID_Kopano_CalDav,
    'PSETID_AirSync': PSETID_AirSync,
    'PSETID_Sharing': PSETID_Sharing,
    'PSETID_PostRss': PSETID_PostRss,
    'PSETID_UnifiedMessaging': PSETID_UnifiedMessaging,
    'PSETID_CONTACT_FOLDER_RECIPIENT': PSETID_CONTACT_FOLDER_RECIPIENT,
    'PSETID_ZMT': PSETID_ZMT,
    'PSETID_CalendarAssistant': PSETID_CalendarAssistant,
    'PSETID_KC': PSETID_KC,
    'PS_PUBLIC_STRINGS': PS_PUBLIC_STRINGS,
}

# XXX copied from common/ECDefs.h
def OBJECTCLASS(__type, __class):
    return (__type << 16) | (__class & 0xFFFF)

OBJECTTYPE_MAILUSER = 1
OBJECTTYPE_CONTAINER = 4
ACTIVE_USER = OBJECTCLASS(OBJECTTYPE_MAILUSER, 1)
NONACTIVE_USER = OBJECTCLASS(OBJECTTYPE_MAILUSER, 2)
CONTAINER_COMPANY = OBJECTCLASS(OBJECTTYPE_CONTAINER, 1)

# XXX copied from msr/main.py
MUIDECSAB = DEFINE_GUID(
    0x50a921ac, 0xd340, 0x48ee, 0xb3, 0x19, 0xfb, 0xa7, 0x53, 0x30, 0x44, 0x25
)
def DEFINE_ABEID(type, id):
    fmt = "4B16s3I4B"
    return struct.pack(fmt, 0, 0, 0, 0, MUIDECSAB, 0, type, id, 0, 0, 0, 0)
EID_EVERYONE = DEFINE_ABEID(MAPI_DISTLIST, 1)

ADDR_PROPS = [
    (Tags.PR_ADDRTYPE_W, Tags.PR_EMAIL_ADDRESS_W, Tags.PR_ENTRYID,
     Tags.PR_DISPLAY_NAME_W, Tags.PR_SEARCH_KEY),
    (Tags.PR_SENDER_ADDRTYPE_W, Tags.PR_SENDER_EMAIL_ADDRESS_W,
     Tags.PR_SENDER_ENTRYID, Tags.PR_SENDER_NAME_W,
     Tags.PR_SENDER_SEARCH_KEY),
    (Tags.PR_RECEIVED_BY_ADDRTYPE_W,
     Tags.PR_RECEIVED_BY_EMAIL_ADDRESS_W, Tags.PR_RECEIVED_BY_ENTRYID,
     Tags.PR_RECEIVED_BY_NAME_W, Tags.PR_RECEIVED_BY_SEARCH_KEY),
    (Tags.PR_ORIGINAL_SENDER_ADDRTYPE_W,
     Tags.PR_ORIGINAL_SENDER_EMAIL_ADDRESS_W,
     Tags.PR_ORIGINAL_SENDER_ENTRYID, Tags.PR_ORIGINAL_SENDER_NAME_W,
     Tags.PR_ORIGINAL_SENDER_SEARCH_KEY),
    (Tags.PR_ORIGINAL_AUTHOR_ADDRTYPE_W,
     Tags.PR_ORIGINAL_AUTHOR_EMAIL_ADDRESS_W,
     Tags.PR_ORIGINAL_AUTHOR_ENTRYID, Tags.PR_ORIGINAL_AUTHOR_NAME_W,
     Tags.PR_ORIGINAL_AUTHOR_SEARCH_KEY),
    (Tags.PR_SENT_REPRESENTING_ADDRTYPE_W,
     Tags.PR_SENT_REPRESENTING_EMAIL_ADDRESS_W,
     Tags.PR_SENT_REPRESENTING_ENTRYID,
     Tags.PR_SENT_REPRESENTING_NAME_W,
     Tags.PR_SENT_REPRESENTING_SEARCH_KEY),
    (Tags.PR_RCVD_REPRESENTING_ADDRTYPE_W,
     Tags.PR_RCVD_REPRESENTING_EMAIL_ADDRESS_W,
     Tags.PR_RCVD_REPRESENTING_ENTRYID,
     Tags.PR_RCVD_REPRESENTING_NAME_W,
     Tags.PR_RCVD_REPRESENTING_SEARCH_KEY),
]

# Common/RecurrenceState.h
# Defines for recurrence exceptions
ARO_SUBJECT = 0x0001
ARO_MEETINGTYPE = 0x0002
ARO_REMINDERDELTA = 0x0004
ARO_REMINDERSET = 0x0008
ARO_LOCATION = 0x0010
ARO_BUSYSTATUS = 0x0020
ARO_ATTACHMENT = 0x0040
ARO_SUBTYPE = 0x0080
ARO_APPTCOLOR = 0x0100
ARO_EXCEPTIONAL_BODY = 0x0200

# location of entryids in PR_IPM_OL2007_ENTRYIDS
RSF_PID_RSS_SUBSCRIPTION = 0x8001
RSF_PID_TODO_SEARCH = 0x8004
RSF_PID_SUGGESTED_CONTACTS = 0x8008

# XXX should we make the names pretty much identical, except for case?
ENGLISH_FOLDER_MAP = {
    'Inbox': 'inbox',
    'Drafts': 'drafts',
    'Outbox': 'outbox',
    'Sent Items': 'sentmail',
    'Deleted Items': 'wastebasket',
    'Junk E-mail': 'junk',
    'Calendar': 'calendar',
    'Contacts': 'contacts',
    'Tasks': 'tasks',
    'Notes': 'notes',
}

UNESCAPED_SLASH_RE = re.compile(r'(?<!\\)/')

RIGHT_NAME = {
    0x1: 'read_items',
    0x2: 'create_items',
    0x80: 'create_subfolders',
    0x8: 'edit_own',
    0x20: 'edit_all',
    0x10: 'delete_own',
    0x40: 'delete_all',
    0x100: 'folder_owner',
    0x200: 'folder_contact',
    0x400: 'folder_visible',
}

NAME_RIGHT = dict((b, a) for (a, b) in RIGHT_NAME.items())

URGENCY = {
    0: u'low',
    1: u'normal',
    2: u'high'
}

REV_URGENCY = dict((b, a) for (a, b) in URGENCY.items())

ASF_MEETING = 1
ASF_RECEIVED = 2
ASF_CANCELED = 4
