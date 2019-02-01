# SPDX-License-Identifier: AGPL-3.0-only
import MAPI
from MAPI.Util import *

import sys
if sys.version_info[0] > 2:
    unicode = str

def GetStores(session, users = None, flags = MDB_WRITE):
    # Get rid of potential MAPI_UNICODE flag, which was allowed in
    # previous versions to specify the string typ of the users list,
    # which is now autodetected.
    flags &= ~MAPI_UNICODE
    ems = GetDefaultStore(session).QueryInterface(IID_IExchangeManageStore)
    if users is None:
        users = GetUserList(session, flags = MAPI_UNICODE)
    elif isinstance(users, basestring):
        users = [users]

    for user in users:
        try:
            if isinstance(user, unicode):
                fMapiUnicode = MAPI_UNICODE
            else:
                fMapiUnicode = 0
            storeid = ems.CreateStoreEntryID(None, user, fMapiUnicode)
            store = session.OpenMsgStore(0, storeid, IID_IMsgStore, flags)
        except MAPIErrorNotFound:
            continue
        yield store


def GetFolders(store, **kwargs):
    rootid = None   # Root folder
    flags = MAPI_BEST_ACCESS
    depth = 0
    if 'rootid' in kwargs: rootid = kwargs['rootid']
    if 'receivefolder' in kwargs: rootid = store.GetReceiveFolder(kwargs['receivefolder'], 0)[0]
    if 'flags' in kwargs: flags = kwargs['flags']
    if 'depth' in kwargs: depth = kwargs['depth']

    root = store.OpenEntry(rootid, IID_IMAPIFolder, flags)

    if depth == 1:
        table = root.GetHierarchyTable(0)
    else:
        table = root.GetHierarchyTable(CONVENIENT_DEPTH)
        if depth != 0:
            table.Restrict(SPropertyRestriction(RELOP_LE, PR_DEPTH, SPropValue(PR_DEPTH, depth)), TBL_BATCH)
    table.SetColumns([PR_ENTRYID, PR_DEPTH], TBL_BATCH)

    while True:
        rows = table.QueryRows(1, 0)
        if len(rows) == 0:
            break
        folder = root.OpenEntry(rows[0][0].Value, IID_IMAPIFolder, flags)
        yield (folder, rows[0][1].Value)
