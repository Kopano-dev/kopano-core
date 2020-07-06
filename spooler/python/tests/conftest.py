# SPDX-License-Identifier: AGPL-3.0-only

import pathlib
import os

import pytest

from MAPI import (ATTACH_BY_VALUE, MAPI_MODIFY, DELETE_HARD_DELETE,
                  DEL_FOLDERS, DEL_MESSAGES, FOLDER_GENERIC, OPEN_IF_EXISTS)
from MAPI.Struct import SPropValue
from MAPI.Tags import (PR_ENTRYID, PR_MESSAGE_CLASS, PR_SUBJECT,
                       PR_ATTACH_METHOD, PR_ATTACH_DATA_BIN,
                       PR_DISPLAY_NAME_W, PR_ATTACH_MIME_TAG_W,
                       PR_IPM_PUBLIC_FOLDERS_ENTRYID)
from MAPI.Util import OpenECSession, GetDefaultStore, GetPublicStore


class FakeLogger(object):
    def log(self, lvl, msg):
        print(msg)
        pass

    def logDebug(self, msg):
        print(msg)
        pass

    def logInfo(self, msg):
        print(msg)
        pass

    def logNotice(self, msg):
        print(msg)
        pass

    def logWarn(self, msg):
        print(msg)
        pass

    def logError(self, msg):
        print(msg)
        pass

    def logFatal(self, msg):
        print(msg)
        pass


@pytest.fixture
def logger():
    return FakeLogger()


@pytest.fixture
def session():
    user = os.getenv('KOPANO_TEST_USER')
    password = os.getenv('KOPANO_TEST_PASSWORD')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket)


@pytest.fixture
def ab(session):
    return session.OpenAddressBook(0, None, 0)


@pytest.fixture
def store(session):
    return GetDefaultStore(session)


@pytest.fixture
def root(store):
    return store.OpenEntry(None, None, MAPI_MODIFY)


@pytest.fixture
def message(root):
    message = root.CreateMessage(None, 0)

    yield message

    eid = message.GetProps([PR_ENTRYID], 0)[0]
    root.DeleteMessages([eid.Value], 0, None, DELETE_HARD_DELETE)


@pytest.fixture
def publicstore(session):
    return GetPublicStore(session)


@pytest.fixture
def publicroot(publicstore):
    rootid = publicstore.GetProps([PR_IPM_PUBLIC_FOLDERS_ENTRYID], 0)[0]
    yield publicstore.OpenEntry(rootid.Value, None, 0)


@pytest.fixture
def publicfolder(publicroot):
    folder = publicroot.CreateFolder(FOLDER_GENERIC, b'testfolder', b'', None,  OPEN_IF_EXISTS)
    folderid = folder.GetProps([PR_ENTRYID], 0)[0].Value
    yield folder
    publicroot.DeleteFolder(folderid, 0, None, DEL_FOLDERS | DEL_MESSAGES)


@pytest.fixture
def bmpmessage(message, bmp):
    message.SetProps([SPropValue(PR_MESSAGE_CLASS, b'IPM.Note'),
                      SPropValue(PR_SUBJECT, b'BMP message')])

    _, attach = message.CreateAttach(None, 0)
    attach.SetProps([SPropValue(PR_ATTACH_METHOD, ATTACH_BY_VALUE),
                     SPropValue(PR_ATTACH_DATA_BIN, bmp),
                     SPropValue(PR_DISPLAY_NAME_W, 'bmp image'),
                     SPropValue(PR_ATTACH_MIME_TAG_W, 'image/bmp')])
    attach.SaveChanges(0)
    message.SaveChanges(0)

    return message


@pytest.fixture
def pluginpath():
    return os.getenv('PLUGIN_PATH')


@pytest.fixture
def bmp():
    bmpfile = pathlib.Path(__file__).parent.absolute().joinpath('data/test.bmp')
    return open(bmpfile.absolute(), 'rb').read()
