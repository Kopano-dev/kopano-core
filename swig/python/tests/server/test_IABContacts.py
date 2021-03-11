import codecs
import os

import pytest

from MAPI import (MAPI_UNICODE, PT_ERROR, FOLDER_GENERIC, DEL_FOLDERS, DEL_MESSAGES, DELETE_HARD_DELETE, MAPI_BEST_ACCESS,
                  KEEP_OPEN_READWRITE, RELOP_EQ, MAPI_MAILUSER, CONVENIENT_DEPTH, ROW_ADD, ROWLIST_REPLACE, MAPI_MODIFY)
from MAPI.Util import GetDefaultStore, GetPublicStore, PpropFindProp, SPropValue, SPropertyRestriction, OpenECSession
from MAPI.Defs import PROP_TYPE
from MAPI.Util.AddressBook import MUIDECSAB
from MAPI.Struct import MAPIError, ROWENTRY
from MAPI.Tags import (PR_AB_PROVIDER_ID, PR_ENTRYID, PR_IPM_CONTACT_ENTRYID,
                       PR_DISPLAY_NAME_W, PR_DEPTH, PR_IPM_PUBLIC_FOLDERS_ENTRYID,
                       PR_ZC_CONTACT_FOLDER_ENTRYIDS, PR_ZC_CONTACT_FOLDER_NAMES_W,
                       PR_ZC_CONTACT_STORE_ENTRYIDS, PR_ADDRTYPE, PR_BODY, PR_LOCALITY,
                       PR_STATE_OR_PROVINCE, PR_COMPANY_NAME, PR_BUSINESS_FAX_NUMBER,
                       PR_GIVEN_NAME, PR_MIDDLE_NAME, PR_NORMALIZED_SUBJECT, PR_MIDDLE_NAME,
                       PR_TITLE, PR_TRANSMITABLE_DISPLAY_NAME, PR_OBJECT_TYPE,
                       PR_MESSAGE_CLASS, PR_DISPLAY_NAME, PR_ACCOUNT_W,
                       PR_MEMBER_ENTRYID, PR_ACL_TABLE, IID_IExchangeModifyTable, PR_MEMBER_RIGHTS,
                       PR_MAILBOX_OWNER_ENTRYID,
                       ecRightsFolderVisible, ecRightsReadAny, pbGlobalProfileSectionGuid,
                       IID_IMAPIFolder)

# TODO: define in python-mapi
CONTACTS_GUID = codecs.decode('727f0430e3924fdab86ae52a7fe46571', 'hex')
# ULONG(flags) + GUID + BYTE(type)
WRAPPED_ENTRYID_PREFIX = codecs.decode('00000000C091ADD3519DCF11A4A900AA0047FAA4', 'hex')
WRAPPED_EID_TYPE_PERSONAL_DISTLIST = b'\xB5'
WRAPPED_EID_TYPE_LOCAL_DISTLIST = b'\xC3'


@pytest.fixture
def providersession():
    user = os.getenv('KOPANO_TEST_USER')
    password = os.getenv('KOPANO_TEST_PASSWORD')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket, providers=[b'ZARAFA6', b'ZCONTACTS'])

@pytest.fixture
def store(providersession):
    return GetDefaultStore(providersession)


@pytest.fixture
def publicstore(providersession):
    return GetPublicStore(providersession)


@pytest.fixture
def providerstore(providersession):
    return GetDefaultStore(providersession)


@pytest.fixture
def ab(providersession):
    return providersession.OpenAddressBook(0, None, 0)


@pytest.fixture
def abroot(ab):
    return ab.OpenEntry(None, None, 0)


def add_folder_to_provider(session, store_eid, folder_eid, folder_name):
    section = session.OpenProfileSection(pbGlobalProfileSectionGuid, None, 0)
    props = section.GetProps([PR_ZC_CONTACT_STORE_ENTRYIDS, PR_ZC_CONTACT_FOLDER_ENTRYIDS, PR_ZC_CONTACT_FOLDER_NAMES_W], 0)

    if PROP_TYPE(props[0].ulPropTag) == PT_ERROR:
        props[0].Value = [store_eid]
    else:
        props[0].Value.append(store_eid)

    props[0].ulPropTag = PR_ZC_CONTACT_STORE_ENTRYIDS

    if PROP_TYPE(props[1].ulPropTag) == PT_ERROR:
        props[1].Value = [folder_eid]
    else:
        props[1].Value.append(folder_eid)

    props[1].ulPropTag = PR_ZC_CONTACT_FOLDER_ENTRYIDS

    if PROP_TYPE(props[2].ulPropTag) == PT_ERROR:
        props[2].Value = [folder_name]
    else:
        props[2].Value.append(folder_name)

    props[2].ulPropTag = PR_ZC_CONTACT_FOLDER_NAMES_W

    section.SetProps(props)


@pytest.fixture
def contacts(providersession, store):
    root = store.OpenEntry(None, None, 0)
    contact_eid = root.GetProps([PR_IPM_CONTACT_ENTRYID], 0)[0].Value
    contacts = store.OpenEntry(contact_eid, None, MAPI_BEST_ACCESS)
    yield contacts
    contacts.EmptyFolder(DELETE_HARD_DELETE, None, 0)


@pytest.fixture
def gab_contact_folder(providersession, store):
    root = store.OpenEntry(None, None, 0)
    store_eid = store.GetProps([PR_ENTRYID], 0)[0].Value
    contact_eid = root.GetProps([PR_IPM_CONTACT_ENTRYID], 0)[0].Value
    add_folder_to_provider(providersession, store_eid, contact_eid, 'User Folder')


@pytest.fixture
def custom_contact_folder(providersession, store):
    root = store.OpenEntry(None, None, 0)
    custom = root.CreateFolder(FOLDER_GENERIC, b'custom', None, IID_IMAPIFolder, 0)
    yield custom
    custom_eid = custom.GetProps([PR_ENTRYID], 0)[0].Value
    root.DeleteFolder(custom_eid, 0, None, DEL_FOLDERS | DEL_MESSAGES)


@pytest.fixture
def public_contact_folder(providersession, publicstore):
    peid = publicstore.GetProps([PR_ENTRYID], 0)[0]
    pubfolders = providersession.OpenEntry(publicstore.GetProps([PR_IPM_PUBLIC_FOLDERS_ENTRYID], 0)[0].Value, None, 0)
    pubfolder_eid = pubfolders.GetProps([PR_ENTRYID], 0)[0]
    pubfolder = pubfolders.CreateFolder(FOLDER_GENERIC, b'zcontacs', b'', None, 0)
    eid = pubfolder.GetProps([PR_ENTRYID], 0)[0].Value
    yield pubfolder
    pubfolders.DeleteFolder(eid, 0, None, DEL_FOLDERS | DEL_MESSAGES)


@pytest.fixture
def gab_public_contact(providersession, publicstore, public_contact_folder):
    peid = publicstore.GetProps([PR_ENTRYID], 0)[0].Value
    pubfolder_eid = public_contact_folder.GetProps([PR_ENTRYID], 0)[0].Value
    add_folder_to_provider(providersession, peid, pubfolder_eid, 'Public Contacts')


@pytest.fixture
def gab_custom_folder(providersession, custom_contact_folder, store):
    store_eid = store.GetProps([PR_ENTRYID], 0)[0].Value
    custom_eid = custom_contact_folder.GetProps([PR_ENTRYID], 0)[0].Value
    add_folder_to_provider(providersession, store_eid, custom_eid, 'Custom Folder')


@pytest.fixture
def gab_contacts(ab, abroot):
    return assert_get_contact_folder(ab, abroot)


@pytest.fixture
def incomplete_contact(contacts):
    contact = contacts.CreateMessage(None, 0)
    contact.SetProps([SPropValue(PR_ADDRTYPE, b'SMTP'), SPropValue(PR_BODY, b'description'),
                     SPropValue(PR_LOCALITY, b'city'), SPropValue(PR_STATE_OR_PROVINCE, b'ZH'),
                     SPropValue(PR_BUSINESS_FAX_NUMBER, b'+31 FAX'), SPropValue(PR_COMPANY_NAME, b'company'),
                     SPropValue(PR_GIVEN_NAME, b'Jimmy'), SPropValue(PR_MIDDLE_NAME, b'Two-Fingers'),
                     SPropValue(PR_NORMALIZED_SUBJECT, b'a display name'), SPropValue(PR_TITLE, b'Title'),
                     SPropValue(PR_TRANSMITABLE_DISPLAY_NAME, b'7bit name'),
                     SPropValue(PR_MESSAGE_CLASS, b'IPM.Contact'),
                     SPropValue(PR_DISPLAY_NAME, b'good old display name')
                     # no email address and arraytype properties set: invalid contact will not be shown in addressbook
    ])
    contact.SaveChanges(0)


@pytest.fixture
def single_contact(ab, contacts):
    contact = contacts.CreateMessage(None, 0)
    # TODO: remove hardcoded named props
    contact.SetProps([SPropValue(PR_ADDRTYPE, b'SMTP'), SPropValue(PR_BODY, b'description'),
                     SPropValue(PR_LOCALITY, b'city'), SPropValue(PR_STATE_OR_PROVINCE, b'ZH'),
                     SPropValue(PR_BUSINESS_FAX_NUMBER, b'+31 FAX'), SPropValue(PR_COMPANY_NAME, b'company'),
                     SPropValue(0x8130001E, b'my name here'), SPropValue(0x8132001E, b'SMTP'),  # address:32896 / address:32898
                     SPropValue(0x8133001E, b'kontakt@external.com'), SPropValue(0x8134001E, b'Kontakt Original Display'), # address:32899 / address:32900
                     SPropValue(0x81350102, ab.CreateOneOff(b'Kontakt Original Display', b'SMTP', b'kontakt@external.com', 0)), #
                     SPropValue(PR_GIVEN_NAME, b'Jimmy'), SPropValue(PR_MIDDLE_NAME, b'Two-Fingers'),
                     SPropValue(PR_NORMALIZED_SUBJECT, b'a display name'), SPropValue(PR_TITLE, b'Title'),
                     SPropValue(PR_TRANSMITABLE_DISPLAY_NAME, b'7bit name'),
                     SPropValue(PR_MESSAGE_CLASS, b'IPM.Contact'),
                     SPropValue(PR_DISPLAY_NAME, b'good old display name'),
                     SPropValue(0x80D81003, [0]), SPropValue(0x80D90003, 1)  # address:32808 / address:32809
    ])
    contact.SaveChanges(KEEP_OPEN_READWRITE)
    return contact


@pytest.fixture
def public_contact(ab, public_contact_folder):
    contact = public_contact_folder.CreateMessage(None, 0)
    # TODO: remove hardcoded named props
    contact.SetProps([SPropValue(PR_ADDRTYPE, b'SMTP'), SPropValue(PR_BODY, b'description'),
                     SPropValue(PR_LOCALITY, b'city'), SPropValue(PR_STATE_OR_PROVINCE, b'ZH'),
                     SPropValue(PR_BUSINESS_FAX_NUMBER, b'+31 FAX'), SPropValue(PR_COMPANY_NAME, b'company'),
                     SPropValue(0x8130001E, b'my name here'), SPropValue(0x8132001E, b'SMTP'),  # address:32896 / address:32898
                     SPropValue(0x8133001E, b'kontakt@external.com'), SPropValue(0x8134001E, b'Kontakt Original Display'), # address:32899 / address:32900
                     SPropValue(0x81350102, ab.CreateOneOff(b'Kontakt Original Display', b'SMTP', b'kontakt@external.com', 0)), #
                     SPropValue(PR_GIVEN_NAME, b'Jimmy'), SPropValue(PR_MIDDLE_NAME, b'Two-Fingers'),
                     SPropValue(PR_NORMALIZED_SUBJECT, b'a display name'), SPropValue(PR_TITLE, b'Title'),
                     SPropValue(PR_TRANSMITABLE_DISPLAY_NAME, b'7bit name'),
                     SPropValue(PR_MESSAGE_CLASS, b'IPM.Contact'),
                     SPropValue(PR_DISPLAY_NAME, b'good old display name'),
                     SPropValue(0x80D81003, [0]), SPropValue(0x80D90003, 1)  # address:32808 / address:32809
    ])
    contact.SaveChanges(KEEP_OPEN_READWRITE)
    return contact


@pytest.fixture
def fax_contact(ab, single_contact):
    # fax contact, what outlook 2007 sets when editing a business fax field
    single_contact.SetProps([SPropValue(0x8172001E, b'FAX'),
                             SPropValue(0x8173001E, b'Kontakt Name@+31 (010) 1234567'), SPropValue(0x8174001E, b'Business Fax Original Display'),
                             SPropValue(0x81750102, ab.CreateOneOff(b'Business Fax Original Display', b'FAX', b'Kontakt Name@+31 (010) 1234567', 0)),
                             SPropValue(0x80D81003, [0, 3]), SPropValue(0x80D90003, 7)  # address:32808 / address:32809
    ])
    single_contact.SaveChanges(0)


@pytest.fixture
def multi_contact(ab, single_contact):
    single_contact.SetProps([SPropValue(0x8140001E, b'my second name here'), SPropValue(0x8142001E, b'SMTP'),
                     SPropValue(0x8143001E, b'kontakt@somewhere-else.com'), SPropValue(0x8144001E, b'Kontakt Original Display 2'),
                     SPropValue(0x81450102, ab.CreateOneOff(b'Kontakt Original Display 2', b'SMTP', b'kontakt@somewhere-else.com', 0)),
                     SPropValue(0x80D81003, [0,1]), SPropValue(0x80D90003, 3)
    ])
    single_contact.SaveChanges(0)


@pytest.fixture
def gab_distlist(ab, contacts):
    '''distlist with two GAB members'''
    gabdir = ab.GetDefaultDir()
    gab = ab.OpenEntry(gabdir, None, 0)
    table = gab.GetContentsTable(MAPI_UNICODE)
    table.Restrict(SPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, SPropValue(PR_OBJECT_TYPE, MAPI_MAILUSER)), 0)
    table.SetColumns([PR_ENTRYID, PR_ACCOUNT_W, PR_DISPLAY_NAME_W], 0)
    rows = table.QueryRows(2, 0)

    wrap = WRAPPED_ENTRYID_PREFIX + WRAPPED_EID_TYPE_PERSONAL_DISTLIST
    distlist = contacts.CreateMessage(None, 0)
    distlist.SetProps([SPropValue(PR_DISPLAY_NAME, b'My GAB Group'),
                      SPropValue(PR_MESSAGE_CLASS, b'IPM.DistList'),
                      SPropValue(0x81041102, [ab.CreateOneOff(rows[0][2].Value, 'KOPANO', rows[0][1].Value, MAPI_UNICODE),
                                              ab.CreateOneOff(rows[1][2].Value, 'KOPANO', rows[1][1].Value, MAPI_UNICODE)]),
                      SPropValue(0x81051102, [wrap + rows[0][0].Value, wrap + rows[1][0].Value]),
    ])
    distlist.SaveChanges(0)


@pytest.fixture
def local_distlist(ab, contacts, single_contact):
    wrap = WRAPPED_ENTRYID_PREFIX + WRAPPED_EID_TYPE_LOCAL_DISTLIST
    eid = single_contact.GetProps([PR_ENTRYID], 0)[0]
    distlist = contacts.CreateMessage(None, 0)
    distlist.SetProps([SPropValue(PR_DISPLAY_NAME, b'My GAB Group'),
                      SPropValue(PR_MESSAGE_CLASS, b'IPM.DistList'),
                      SPropValue(0x81041102, [ab.CreateOneOff('dummy', 'KOPANO', 'dummy@localhost', MAPI_UNICODE)]),
                      SPropValue(0x81051102, [wrap + eid.Value]),
    ])
    distlist.SaveChanges(0)


@pytest.fixture
def gab_shared_contacts(providersession):
    user = os.getenv('KOPANO_TEST_USER2')
    password = os.getenv('KOPANO_TEST_PASSWORD2')
    socket = os.getenv('KOPANO_SOCKET')
    session = OpenECSession(user, password, socket, providers=[b'ZARAFA6', b'ZCONTACTS'])

    store = GetDefaultStore(session)
    seid = store.GetProps([PR_ENTRYID], 0)[0]
    usereid = store.GetProps([PR_MAILBOX_OWNER_ENTRYID], 0)[0].Value

    root = store.OpenEntry(None, None, 0)
    ceid = root.GetProps([PR_IPM_CONTACT_ENTRYID], 0)[0].Value
    sharedfolder = session.OpenEntry(ceid, None, 0)
    shared_eid = sharedfolder.GetProps([PR_ENTRYID], 0)[0]

    add_folder_to_provider(providersession, seid.Value, shared_eid.Value, 'Shared Contacts')

    acltab = sharedfolder.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, MAPI_MODIFY)
    rowlist = [ROWENTRY(ROW_ADD, [SPropValue(PR_MEMBER_ENTRYID, usereid), SPropValue(PR_MEMBER_RIGHTS, ecRightsFolderVisible | ecRightsReadAny)])]
    acltab.ModifyTable(ROWLIST_REPLACE, rowlist)
    yield sharedfolder
    # Reset ACL's
    acltab.ModifyTable(ROWLIST_REPLACE, [])


def assert_get_contact_folder(ab, abroot):
    hierarchy = abroot.GetHierarchyTable(MAPI_UNICODE)
    eid, store_eid = None, None
    for row in hierarchy.QueryRows(-1, 0):
        prop = PpropFindProp(row, PR_AB_PROVIDER_ID)
        if prop.Value == CONTACTS_GUID:
            store_eid = PpropFindProp(row, PR_AB_PROVIDER_ID)
            eid = PpropFindProp(row, PR_ENTRYID)
            break

    assert store_eid
    assert eid

    return  ab.OpenEntry(eid.Value, None, 0)


def get_gab_contacts_rows(gab_contacts):
    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    eid = PpropFindProp(rows[0], PR_ENTRYID)
    usercont = gab_contacts.OpenEntry(eid.Value, None, 0)
    contents = usercont.GetContentsTable(MAPI_UNICODE)
    return contents.QueryRows(-1, 0)


def test_ab_root_hierarchy(abroot):
    hierarchy = abroot.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    assert len(rows) == 3

    providers = 0
    for row in rows:
        prop = PpropFindProp(row, PR_AB_PROVIDER_ID)
        assert prop
        if prop.Value == MUIDECSAB:
            providers |= 1
        if prop.Value == CONTACTS_GUID:
            providers |= 2
    assert providers  == (1 | 2)


def test_ab_folder_hierarchy(gab_contact_folder, ab, abroot):
    gab_contacts = assert_get_contact_folder(ab, abroot)
    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    assert len(rows) == 1
    display_name = PpropFindProp(rows[0], PR_DISPLAY_NAME_W)
    assert display_name.Value == 'User Folder'


def test_ab_folder_multi_hierarchy(gab_contact_folder, gab_custom_folder, ab, abroot):
    gab_contacts = assert_get_contact_folder(ab, abroot)
    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    assert len(rows) == 2

    display_name = PpropFindProp(rows[0], PR_DISPLAY_NAME_W)
    assert display_name.Value == 'User Folder'

    display_name = PpropFindProp(rows[1], PR_DISPLAY_NAME_W)
    assert display_name.Value == 'Custom Folder'


def test_ab_folder_incomplete_contact(gab_contact_folder, gab_contacts, incomplete_contact):
    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    eid = PpropFindProp(rows[0], PR_ENTRYID)
    usercont = gab_contacts.OpenEntry(eid.Value, None, 0)
    contents = usercont.GetContentsTable(MAPI_UNICODE)
    rows = contents.QueryRows(-1, 0)
    # no contact here, since it was invalid!
    assert len(rows) == 0


def test_ab_folder_single_contact(gab_contact_folder, gab_contacts, single_contact):
    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    eid = PpropFindProp(rows[0], PR_ENTRYID)
    usercont = gab_contacts.OpenEntry(eid.Value, None, 0)
    contents = usercont.GetContentsTable(MAPI_UNICODE)
    rows = contents.QueryRows(-1, 0)
    assert len(rows) == 1
    eid = PpropFindProp(rows[0], PR_ENTRYID)
    contact = usercont.OpenEntry(eid.Value, None, 0)
    assert contact

def test_ab_folder_multi_contact(gab_contact_folder, gab_contacts, multi_contact):
    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    eid = PpropFindProp(rows[0], PR_ENTRYID)
    usercont = gab_contacts.OpenEntry(eid.Value, None, 0)
    contents = usercont.GetContentsTable(MAPI_UNICODE)
    rows = contents.QueryRows(-1, 0)
    assert len(rows) == 2


def test_ab_folder_fax_contact(gab_contact_folder, gab_contacts, fax_contact):
    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    eid = PpropFindProp(rows[0], PR_ENTRYID)
    usercont = gab_contacts.OpenEntry(eid.Value, None, 0)
    contents = usercont.GetContentsTable(MAPI_UNICODE)
    rows = contents.QueryRows(-1, 0)
    assert len(rows) == 2


def test_ab_folder_gab_distlist(gab_contact_folder, gab_contacts, gab_distlist):
    '''DistList with GAB contacts'''

    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    eid = PpropFindProp(rows[0], PR_ENTRYID)
    usercont = gab_contacts.OpenEntry(eid.Value, None, 0)
    contents = usercont.GetContentsTable(MAPI_UNICODE)
    rows = contents.QueryRows(-1, 0)
    assert len(rows) == 1


def test_ab_folder_local_distlist(gab_contact_folder, gab_contacts, local_distlist):
    '''DistList with local contacts'''

    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    eid = PpropFindProp(rows[0], PR_ENTRYID)
    usercont = gab_contacts.OpenEntry(eid.Value, None, 0)
    contents = usercont.GetContentsTable(MAPI_UNICODE)
    rows = contents.QueryRows(-1, 0)
    # Local distlist and contact
    assert len(rows) == 2


def test_ab_getsearchpath(ab):
    searchpath = ab.GetSearchPath(MAPI_UNICODE)
    assert len(searchpath) == 1
    assert PpropFindProp(searchpath[0], PR_DISPLAY_NAME_W) == SPropValue(0x3001001F, 'Global Address Book')


def test_ab_contact_searchpath(ab, gab_contact_folder):
    searchpath = ab.GetSearchPath(MAPI_UNICODE)
    assert len(searchpath) == 2
    assert PpropFindProp(searchpath[0], PR_DISPLAY_NAME_W) == SPropValue(0x3001001F, 'Global Address Book')
    assert PpropFindProp(searchpath[1], PR_DISPLAY_NAME_W) == SPropValue(0x3001001F, 'User Folder')


def test_ab_custom_searchpath(ab, gab_contact_folder, gab_custom_folder):
    searchpath = ab.GetSearchPath(MAPI_UNICODE)
    assert len(searchpath) == 3
    assert PpropFindProp(searchpath[0], PR_DISPLAY_NAME_W) == SPropValue(0x3001001F, 'Global Address Book')
    assert PpropFindProp(searchpath[1], PR_DISPLAY_NAME_W) == SPropValue(0x3001001F, 'User Folder')
    assert PpropFindProp(searchpath[2], PR_DISPLAY_NAME_W) == SPropValue(0x3001001F, 'Custom Folder')


def test_ab_depth(abroot, gab_contact_folder):
    hierarchy = abroot.GetHierarchyTable(MAPI_UNICODE | CONVENIENT_DEPTH)
    hierarchy.Restrict(SPropertyRestriction(RELOP_EQ, PR_AB_PROVIDER_ID, SPropValue(PR_AB_PROVIDER_ID, CONTACTS_GUID)), 0)
    rows = hierarchy.QueryRows(-1, 0)
    assert len(rows) == 2

    assert PpropFindProp(rows[0], PR_DISPLAY_NAME_W).Value == 'Kopano Contacts Folders'
    assert PpropFindProp(rows[0], PR_DEPTH).Value == 0

    assert PpropFindProp(rows[1], PR_DISPLAY_NAME_W).Value == 'User Folder'
    assert PpropFindProp(rows[1], PR_DEPTH).Value == 1


def test_resolvenames_single(ab, gab_contact_folder, single_contact):
    names = ab.ResolveName(0, MAPI_UNICODE, None, [[SPropValue(PR_DISPLAY_NAME, b'kontakt')]])
    assert len(names) == 1


def test_resolvenames_multi(ab, gab_contact_folder, multi_contact):
    with pytest.raises(MAPIError) as excinfo:
        ab.ResolveName(0, MAPI_UNICODE, None, [[SPropValue(PR_DISPLAY_NAME, b'kontakt')]])
    assert 'MAPI_E_AMBIGUOUS_RECIP' in str(excinfo)

    names = ab.ResolveName(0, MAPI_UNICODE, None, [[SPropValue(PR_DISPLAY_NAME, b'kontakt@somewhere')]])
    assert len(names) == 1


def test_public_contacts(ab, abroot, gab_public_contact, public_contact):
    gab_contacts = assert_get_contact_folder(ab, abroot)
    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    assert len(rows) == 1
    displayname = PpropFindProp(rows[0], PR_DISPLAY_NAME_W)
    assert displayname.Value == 'Public Contacts'

    eid = PpropFindProp(rows[0], PR_ENTRYID)
    usercont = gab_contacts.OpenEntry(eid.Value, None, 0)
    contents = usercont.GetContentsTable(MAPI_UNICODE)
    rows = contents.QueryRows(-1, 0)
    assert len(rows) == 1


def test_shared_contacts(ab, abroot, gab_shared_contacts):
    gab_contacts = assert_get_contact_folder(ab, abroot)
    hierarchy = gab_contacts.GetHierarchyTable(MAPI_UNICODE)
    rows = hierarchy.QueryRows(-1, 0)
    assert len(rows) == 1
    displayname = PpropFindProp(rows[0], PR_DISPLAY_NAME_W)
    assert displayname.Value == 'Shared Contacts'
