# SPDX-License-Identifier: AGPL-3.0-only
from MAPI.Defs import *
from MAPI.Tags import *
from MAPI.Struct import *

import sys
if sys.version_info[0] > 2:
    unicode = str

MUIDECSAB = DEFINE_GUID(0x50a921ac, 0xd340, 0x48ee, 0xb3, 0x19, 0xfb, 0xa7, 0x53, 0x30, 0x44, 0x25)

def GetGab(session):
    ab = session.OpenAddressBook(0, None, 0)
    root = ab.OpenEntry(None, None, 0)
    table = root.GetHierarchyTable(0)
    table.SetColumns([PR_ENTRYID], TBL_BATCH)
    restriction = SOrRestriction([SPropertyRestriction(RELOP_EQ, PR_DISPLAY_TYPE, SPropValue(PR_DISPLAY_TYPE, DT_GLOBAL)),
                                  SAndRestriction([SExistRestriction(PR_EMS_AB_CONTAINERID),
                                                   SPropertyRestriction(RELOP_EQ, PR_EMS_AB_CONTAINERID, SPropValue(PR_EMS_AB_CONTAINERID, 0))])])
    table.FindRow(restriction, BOOKMARK_BEGINNING, 0)
    eid = table.QueryRows(1, 0)[0][0].Value
    return ab.OpenEntry(eid, None, 0)

def GetCompanyList(session, flags = 0):
    if flags & MAPI_UNICODE:
        columns = [PR_DISPLAY_NAME_W]
    else:
        columns = [PR_DISPLAY_NAME_A]
    companies = []
    gab = GetGab(session)
    table = gab.GetHierarchyTable(0)
    table.SetColumns(columns, TBL_BATCH)
    while True:
        rows = table.QueryRows(50, 0)
        if len(rows) == 0:
            break
        companies.extend((row[0].Value for row in rows))
    return companies

def _GetAbObjectList(container, restriction, flags):
    if flags & MAPI_UNICODE:
        columns = [PR_EMAIL_ADDRESS_W]
    else:
        columns = [PR_EMAIL_ADDRESS_A]
    users = []
    table = container.GetContentsTable(0)
    table.SetColumns(columns, MAPI.TBL_BATCH)
    if restriction:
        table.Restrict(restriction, TBL_BATCH)
    while True:
        rows = table.QueryRows(50, 0)
        if len(rows) == 0:
            break
        users.extend((row[0].Value for row in rows))
    return users

def GetAbObjectList(session, restriction = None, companyname = None, flags = 0):
    gab = GetGab(session)
    table = gab.GetHierarchyTable(0)
    if companyname is None or len(companyname) == 0:
        companyCount = table.GetRowCount(0)
        if companyCount == 0:
            users = _GetAbObjectList(gab, restriction, flags)
        else:
            table.SetColumns([PR_ENTRYID], TBL_BATCH)
            users = []
            for row in table.QueryRows(companyCount, 0):
                company = gab.OpenEntry(row[0].Value, None, 0)
                companyUsers = _GetAbObjectList(company, restriction, flags)
                users.extend(companyUsers)
    else:
        if isinstance(companyname, unicode):
            tag_dispname = PR_DISPLAY_NAME_W
        else:
            tag_dispname = PR_DISPLAY_NAME_A
        table.FindRow(SContentRestriction(FL_FULLSTRING|FL_IGNORECASE, tag_dispname, SPropValue(tag_dispname, companyname)), BOOKMARK_BEGINNING, 0)
        table.SetColumns([PR_ENTRYID], TBL_BATCH)
        row = table.QueryRows(1, 0)[0]
        users = _GetAbObjectList(gab.OpenEntry(row[0].Value, None, 0), restriction, flags)

    return users

def GetUserList(session, companyname = None, flags = 0):
    restriction = SAndRestriction([SPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, SPropValue(PR_OBJECT_TYPE, MAPI_MAILUSER)),
                                   SPropertyRestriction(RELOP_NE, PR_DISPLAY_TYPE, SPropValue(PR_DISPLAY_TYPE, DT_REMOTE_MAILUSER))])
    return GetAbObjectList(session, restriction, companyname, flags)

def GetGroupList(session, companyname = None, flags = 0):
    restriction = SPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, SPropValue(PR_OBJECT_TYPE, MAPI_DISTLIST))
    return GetAbObjectList(session, restriction, companyname, flags)
