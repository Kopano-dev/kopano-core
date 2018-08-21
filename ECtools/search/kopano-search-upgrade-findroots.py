#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-or-later

from MAPI.Util import *

import kopano

"""

as of Kopano-Core 8.2, PR_STORE_SUPPORT_MASK has STORE_SEARCH_OK enabled
for every store. this means that this script needs to run at upgrade time,
to create the findroots and ACLS so that cross-store searches will actually
work.

"""

FINDROOT_RIGHTS = ['folder_visible', 'read_items', 'create_subfolders', 'edit_own', 'delete_own']

def upgrade_findroot(store):
    findroot = store.root.get_folder('FINDER_ROOT')
    if not findroot:
        print('creating FINDER_ROOT for store %s' % store.guid)

        # create findroot
        findroot = store.root.folder('FINDER_ROOT', create=True)

        # set PR_FINDER_ENTRYID # XXX pyko: store.findroot = ..
        entryid = HrGetOneProp(findroot.mapiobj, PR_ENTRYID)
        store.mapiobj.SetProps([SPropValue(PR_FINDER_ENTRYID, entryid.Value)])
        store.mapiobj.SaveChanges(0)

    # add findroot ACL
    findroot.permission(kopano.Group('Everyone'), create=True).rights = FINDROOT_RIGHTS

    # we don't need to update PR_STORE_SUPPORT_MASK as it is hardcoded
    # whether it includes STORE_SEARCH_OK..


def main():
    for store in kopano.stores():
        upgrade_findroot(store)
        if store.archive_store:
            upgrade_findroot(store.archive_store)

if __name__ == '__main__':
    main()
