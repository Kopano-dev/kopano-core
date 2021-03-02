import pytest

from MAPI import (MAPI_ACCESS_MODIFY, MAPI_ACCESS_READ, MAPI_ACCESS_DELETE,
                  MAPI_ACCESS_CREATE_HIERARCHY, MAPI_ACCESS_CREATE_CONTENTS,
                  MAPI_MODIFY, ROW_ADD, MAPI_ACCESS_CREATE_ASSOCIATED, ROWLIST_REPLACE,
                  MAPI_BEST_ACCESS)
from MAPI.Struct import ROWENTRY
from MAPI.Tags import (PR_IPM_SUBTREE_ENTRYID, PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID,
                       PR_DISPLAY_NAME_A, PR_DISPLAY_NAME_W, PR_MEMBER_ENTRYID,
                       PR_MEMBER_RIGHTS, PR_ACCESS, PR_ENTRYID, PR_ACL_TABLE,
                       PR_SUBJECT, IID_IExchangeModifyTable,
                       ecRightsTemplateOwner, ecRightsTemplateReadOnly,
                       ecRightsTemplateSecretary, ecRightsFullControl)
from MAPI.Util import SPropValue, GetDefaultStore, GetPublicStore

FOLDER_ACCESS_OWNER = MAPI_ACCESS_MODIFY | MAPI_ACCESS_READ | MAPI_ACCESS_DELETE | MAPI_ACCESS_CREATE_HIERARCHY | MAPI_ACCESS_CREATE_CONTENTS | MAPI_ACCESS_CREATE_ASSOCIATED
MESSAGE_ACCESS_OWNER = MAPI_ACCESS_MODIFY | MAPI_ACCESS_READ | MAPI_ACCESS_DELETE

FOLDER_ACCESS_FULL = MAPI_ACCESS_READ | MAPI_ACCESS_CREATE_HIERARCHY | MAPI_ACCESS_CREATE_CONTENTS
MESSAGE_ACCESS_FULL = MAPI_ACCESS_MODIFY | MAPI_ACCESS_READ | MAPI_ACCESS_DELETE

FOLDER_ACCESS_SECRETARY = MAPI_ACCESS_READ | MAPI_ACCESS_CREATE_CONTENTS
MESSAGE_ACCESS_SECRETARY = MAPI_ACCESS_MODIFY | MAPI_ACCESS_READ | MAPI_ACCESS_DELETE

FOLDER_ACCESS_READONLY = MAPI_ACCESS_READ
MESSAGE_ACCESS_READONLY = MAPI_ACCESS_READ


@pytest.mark.parametrize(
    "kind,rights,folder_access,message_access",
    [
        ('OWNER', ecRightsTemplateOwner, FOLDER_ACCESS_OWNER, MESSAGE_ACCESS_OWNER),
        ('FULL', ecRightsFullControl, FOLDER_ACCESS_FULL, MESSAGE_ACCESS_FULL),
        ('SECRETARY', ecRightsTemplateSecretary, FOLDER_ACCESS_SECRETARY, MESSAGE_ACCESS_SECRETARY),
        ('READONLY', ecRightsTemplateReadOnly, FOLDER_ACCESS_READONLY, MESSAGE_ACCESS_READONLY)
    ]
)
def test_delegate_permissions_owner(publicfolder, session3, kind, rights, folder_access, message_access):
    print(kind)
    access = publicfolder.GetProps([PR_ACCESS], 0)[0].Value
    defaultAccess = MAPI_ACCESS_MODIFY | MAPI_ACCESS_READ | MAPI_ACCESS_DELETE | MAPI_ACCESS_CREATE_HIERARCHY | MAPI_ACCESS_CREATE_CONTENTS
    assert defaultAccess == access

    store = GetDefaultStore(session3)
    userid = session3.QueryIdentity()

    folderid = publicfolder.GetProps([PR_ENTRYID], 0)[0].Value
    acls = publicfolder.OpenProperty(PR_ACL_TABLE, IID_IExchangeModifyTable, 0, MAPI_MODIFY)
    rowlist = [ROWENTRY(ROW_ADD, [SPropValue(PR_MEMBER_ENTRYID, userid), SPropValue(PR_MEMBER_RIGHTS, rights)])]

    acls.ModifyTable(ROWLIST_REPLACE, rowlist)
    message = publicfolder.CreateMessage(None, 0)
    message.SetProps([SPropValue(PR_SUBJECT, b'acl test')])
    message.SaveChanges(0)
    eid = message.GetProps([PR_ENTRYID], 0)[0].Value
    access = message.GetProps([PR_ACCESS], 0)[0].Value
    assert access == MAPI_ACCESS_MODIFY | MAPI_ACCESS_READ | MAPI_ACCESS_DELETE

    # as user3, open public and get folder access
    public = GetPublicStore(session3)
    folder = public.OpenEntry(folderid, None, MAPI_BEST_ACCESS)
    access = folder.GetProps([PR_ACCESS], 0)[0].Value
    assert access == folder_access

    message = folder.OpenEntry(eid, None, 0)
    access = message.GetProps([PR_ACCESS], 0)[0].Value
    assert access == message_access
