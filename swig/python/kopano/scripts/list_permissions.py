# SPDX-License-Identifier: AGPL-3.0-only
import kopano

#user1 = kopano.user('user1')
#user2 = kopano.user('user2')
#user1.inbox.permission(user2, create=True).rights = ['read_items', 'delete_own']

for store in kopano.stores():
    for permission in store.permissions():
        print 'store', store, permission.member, permission.rights
    for folder in store.folders():
        for permission in folder.permissions():
            print 'folder', store, folder, permission.member, permission.rights



