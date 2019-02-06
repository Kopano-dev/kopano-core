#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-only

# recursively show folder structure and total size

import kopano

server = kopano.server(parse+args=True)

for user in server.users():
    print 'user:', user.name
    if user.store:
        for folder in user.store.root.folders():
            print 'regular: count=%s size=%s %s%s' % (str(folder.count).ljust(8), str(folder.size).ljust(10), folder.depth*'    ', folder.name)
