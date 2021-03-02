import os

import pytest

from MAPI import (MAPIAdminProfiles, KEEP_OPEN_READWRITE, DELETE_HARD_DELETE,
                  MAPI_MODIFY, FOLDER_GENERIC, DEL_FOLDERS, DEL_MESSAGES, IStream,
                  DELETE_HARD_DELETE, DEL_ASSOCIATED, MAPI_UNICODE,
                  FOLDER_SEARCH, MAPI_ASSOCIATED)
from MAPI.Util import (OpenECSession, GetPublicStore, GetDefaultStore, SPropValue,
                       MAPI_BEST_ACCESS, MAPINotifSink)
from MAPI.Tags import (IID_IECTestProtocol, PR_SUBJECT, PR_ENTRYID, PR_IPM_PUBLIC_FOLDERS_ENTRYID,
                       OPEN_IF_EXISTS, IID_IECServiceAdmin, IID_IExchangeManageStore, PR_EMS_AB_CONTAINERID,
                       PR_IPM_SUBTREE_ENTRYID)

import libfreebusy


@pytest.fixture
def adminprof():
    return MAPIAdminProfiles(0)


@pytest.fixture
def adminservice(adminprof):
    name = b't1'
    adminprof.CreateProfile(name, None, 0, 0)

    yield adminprof.AdminServices(name, None, 0, 0)

    adminprof.DeleteProfile(name, 0)


@pytest.fixture
def session():
    user = os.getenv('KOPANO_TEST_USER')
    password = os.getenv('KOPANO_TEST_PASSWORD')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket)


@pytest.fixture
def session3():
    user = os.getenv('KOPANO_TEST_USER3')
    password = os.getenv('KOPANO_TEST_PASSWORD3')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket)


@pytest.fixture
def notifysession():
    user = os.getenv('KOPANO_TEST_USER')
    password = os.getenv('KOPANO_TEST_PASSWORD')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket, flags=0)


@pytest.fixture
def notifystore(notifysession):
    return GetDefaultStore(notifysession)


@pytest.fixture
def notifyinboxid(notifystore):
    return notifystore.GetReceiveFolder(b'IPM', 0)[0]


@pytest.fixture
def notifyinbox(notifystore, notifyinboxid):
    return notifystore.OpenEntry(notifyinboxid, None, MAPI_MODIFY)


@pytest.fixture
def notifymessage(notifyinbox):
    message = notifyinbox.CreateMessage(None, 0)

    yield message

    eid = message.GetProps([PR_ENTRYID], 0)[0]
    notifyinbox.DeleteMessages([eid.Value], 0, None, DELETE_HARD_DELETE)


@pytest.fixture
def sink():
    yield MAPINotifSink()


@pytest.fixture
def session2():
    user = os.getenv('KOPANO_TEST_USER2')
    password = os.getenv('KOPANO_TEST_PASSWORD2')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket)


@pytest.fixture
def adminsession():
    socket = os.getenv('KOPANO_SOCKET')
    return OpenECSession('SYSTEM', '', socket)


@pytest.fixture
def adminstore(adminsession):
    return GetDefaultStore(adminsession)


@pytest.fixture
def store(session):
    return GetDefaultStore(session)


@pytest.fixture
def store2(session2):
    return GetDefaultStore(session2)


@pytest.fixture
def root(store):
    return store.OpenEntry(None, None, MAPI_MODIFY)


@pytest.fixture
def inbox(store):
    inboxeid = store.GetReceiveFolder(b'IPM', 0)[0]
    return store.OpenEntry(inboxeid, None, MAPI_MODIFY)


@pytest.fixture
def addressbook(session):
    return session.OpenAddressBook(0, None, MAPI_UNICODE)


@pytest.fixture
def globaladdressbook(addressbook):
    yield addressbook.OpenEntry(None, None, MAPI_BEST_ACCESS)


@pytest.fixture
def gab(addressbook):
    defaultdir = addressbook.GetDefaultDir()
    return addressbook.OpenEntry(defaultdir, None, 0)


@pytest.fixture
def gabtable(gab):
    return gab.GetContentsTable(0)


# Required for copying 'message' into a new message
@pytest.fixture
def copy(root):
    message = root.CreateMessage(None, 0)

    yield message

    eid = message.GetProps([PR_ENTRYID], 0)[0]
    root.DeleteMessages([eid.Value], 0, None, DELETE_HARD_DELETE)


@pytest.fixture
def message(root):
    message = root.CreateMessage(None, 0)

    yield message

    eid = message.GetProps([PR_ENTRYID], 0)[0]
    root.DeleteMessages([eid.Value], 0, None, DELETE_HARD_DELETE)


@pytest.fixture
def assocmessage(root):
    message = root.CreateMessage(None, MAPI_ASSOCIATED)

    yield message

    eid = message.GetProps([PR_ENTRYID], 0)[0]
    root.DeleteMessages([eid.Value], 0, None, DELETE_HARD_DELETE)


@pytest.fixture
def emptyemail(inbox):
    message = inbox.CreateMessage(None, 0)
    eid = message.GetProps([PR_ENTRYID], 0)[0]
    yield message

    eid = message.GetProps([PR_ENTRYID], 0)[0]
    inbox.DeleteMessages([eid.Value], 0, None, DELETE_HARD_DELETE)


@pytest.fixture
def stream():
    return IStream()


@pytest.fixture
def publicstore(session):
    return GetPublicStore(session)


@pytest.fixture
def publicsubtree(publicstore):
    rootid = publicstore.GetProps([PR_IPM_SUBTREE_ENTRYID], 0)[0]
    yield publicstore.OpenEntry(rootid.Value, None, MAPI_BEST_ACCESS)


@pytest.fixture
def publicroot(publicstore):
    rootid = publicstore.GetProps([PR_IPM_PUBLIC_FOLDERS_ENTRYID], 0)[0]
    yield publicstore.OpenEntry(rootid.Value, None, MAPI_BEST_ACCESS)


@pytest.fixture
def publicfolder(publicroot):
    folder = publicroot.CreateFolder(FOLDER_GENERIC, b'test', b'', None,  OPEN_IF_EXISTS)
    folderid = folder.GetProps([PR_ENTRYID], 0)[0].Value
    yield folder
    publicroot.DeleteFolder(folderid, 0, None, DEL_FOLDERS | DEL_MESSAGES)


@pytest.fixture
def folder(inbox):
    folder = inbox.CreateFolder(FOLDER_GENERIC, b'subfolder', b'', None, OPEN_IF_EXISTS)
    yield folder
    inbox.EmptyFolder(DELETE_HARD_DELETE | DEL_ASSOCIATED, None, 0)


@pytest.fixture
def searchfolder(root):
    folder = root.CreateFolder(FOLDER_SEARCH, b'search', b'', None, 0)
    folderid = folder.GetProps([PR_ENTRYID], 0)[0].Value
    yield folder
    root.DeleteFolder(folderid, 0, None, DEL_FOLDERS | DEL_MESSAGES)


@pytest.fixture
def serviceadmin(adminstore):
    return adminstore.QueryInterface(IID_IECServiceAdmin)


@pytest.fixture
def ema(adminstore):
    return adminstore.QueryInterface(IID_IExchangeManageStore)


@pytest.fixture
def freebusy_user(session, store):
    fb = libfreebusy.IFreeBusySupport()
    fb.Open(session, store, False)
    yield fb
    fb.Close()
