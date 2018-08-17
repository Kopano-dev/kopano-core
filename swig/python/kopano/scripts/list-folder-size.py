#!/usr/bin/python
# SPDX-License-Identifier: AGPL-3.0-only

# recursively show folder structure and total size

import kopano

server = kopano.Server()

for user in server.users(): # checks command-line for -u/--user
    print 'user:', user.name
    if user.store:
        for folder in user.store.root.folders():
            print 'regular: count=%s size=%s %s%s' % (str(folder.count).ljust(8), str(folder.size).ljust(10), folder.depth*'    ', folder.name)
