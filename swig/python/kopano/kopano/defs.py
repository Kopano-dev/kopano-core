"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file for details)
Copyright 2016 - Kopano and its licensors (see LICENSE file for details)
"""

import re
import struct

from MAPI.Util import *

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
    for K in sorted(MAPI.Tags.__dict__, reverse=True):
        if K.startswith('PR_'):
            REV_TAG[MAPI.Tags.__dict__[K]] = K

PS_INTERNET_HEADERS = DEFINE_OLEGUID(0x00020386, 0, 0)
PS_EC_IMAP = DEFINE_GUID(0x00f5f108, 0x8e3f, 0x46c7, 0xaf, 0x72, 0x5e, 0x20, 0x1c, 0x23, 0x49, 0xe7)
PSETID_Archive = DEFINE_GUID(0x72e98ebc, 0x57d2, 0x4ab5, 0xb0, 0xaa, 0xd5, 0x0a, 0x7b, 0x53, 0x1c, 0xb9)
PSETID_Appointment = DEFINE_OLEGUID(0x00062002, 0, 0)
PSETID_Task = DEFINE_OLEGUID(0x00062003, 0, 0)
PSETID_Address = DEFINE_OLEGUID(0x00062004, 0, 0)
PSETID_Common = DEFINE_OLEGUID(0x00062008, 0, 0)
PSETID_Log = DEFINE_OLEGUID(0x0006200A, 0, 0)
PSETID_Note = DEFINE_OLEGUID(0x0006200E, 0, 0)
PSETID_Meeting = DEFINE_GUID(0x6ED8DA90, 0x450B, 0x101B, 0x98, 0xDA, 0x00, 0xAA, 0x00, 0x3F, 0x13, 0x05)

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
}
NAMESPACE_GUID = dict((b, a) for (a, b) in GUID_NAMESPACE.items())

# XXX copied from common/ECDefs.h
def OBJECTCLASS(__type, __class):
    return (__type << 16) | (__class & 0xFFFF)

OBJECTTYPE_MAILUSER = 1
OBJECTTYPE_CONTAINER = 4
ACTIVE_USER = OBJECTCLASS(OBJECTTYPE_MAILUSER, 1)
NONACTIVE_USER = OBJECTCLASS(OBJECTTYPE_MAILUSER, 2)
CONTAINER_COMPANY = OBJECTCLASS(OBJECTTYPE_CONTAINER, 1)

# XXX copied from msr/main.py
MUIDECSAB = DEFINE_GUID(0x50a921ac, 0xd340, 0x48ee, 0xb3, 0x19, 0xfb, 0xa7, 0x53, 0x30, 0x44, 0x25)
def DEFINE_ABEID(type, id):
    return struct.pack("4B16s3I4B", 0, 0, 0, 0, MUIDECSAB, 0, type, id, 0, 0, 0, 0)
EID_EVERYONE = DEFINE_ABEID(MAPI_DISTLIST, 1)

ADDR_PROPS = [
    (PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, PR_ENTRYID, PR_DISPLAY_NAME_W, PR_SEARCH_KEY),
    (PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID, PR_SENDER_NAME_W, PR_SENDER_SEARCH_KEY),
    (PR_RECEIVED_BY_ADDRTYPE_W, PR_RECEIVED_BY_EMAIL_ADDRESS_W, PR_RECEIVED_BY_ENTRYID, PR_RECEIVED_BY_NAME_W, PR_RECEIVED_BY_SEARCH_KEY),
    (PR_ORIGINAL_SENDER_ADDRTYPE_W, PR_ORIGINAL_SENDER_EMAIL_ADDRESS_W, PR_ORIGINAL_SENDER_ENTRYID, PR_ORIGINAL_SENDER_NAME_W, PR_ORIGINAL_SENDER_SEARCH_KEY),
    (PR_ORIGINAL_AUTHOR_ADDRTYPE_W, PR_ORIGINAL_AUTHOR_EMAIL_ADDRESS_W, PR_ORIGINAL_AUTHOR_ENTRYID, PR_ORIGINAL_AUTHOR_NAME_W, PR_ORIGINAL_AUTHOR_SEARCH_KEY),
    (PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_SEARCH_KEY),
    (PR_RCVD_REPRESENTING_ADDRTYPE_W, PR_RCVD_REPRESENTING_EMAIL_ADDRESS_W, PR_RCVD_REPRESENTING_ENTRYID, PR_RCVD_REPRESENTING_NAME_W, PR_RCVD_REPRESENTING_SEARCH_KEY),
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
RSF_PID_SUGGESTED_CONTACTS = 0x8008

ENGLISH_FOLDER_MAP = { # XXX should we make the names pretty much identical, except for case?
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
